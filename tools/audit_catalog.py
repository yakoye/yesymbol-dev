#!/usr/bin/env python3
"""Audit YeSymbol catalog invariants without depending on display headings.

``#`` and ``##`` titles are user-facing text.  They may be renamed, removed,
merged or reordered.  This audit therefore identifies important series from
their Unicode contents and generated structural metadata, never from Chinese or
English heading strings.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence, Set, Tuple

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "data-source" / "catalog.generated.json"
SERIES_MANIFEST = ROOT / "data-source" / "catalog-series-manifest.json"


def load_series() -> Dict[str, List[str]]:
    payload = json.loads(SERIES_MANIFEST.read_text(encoding="utf-8"))
    return {
        name: [chr(int(value, 16)) for value in values]
        for name, values in payload.get("series", {}).items()
    }


def category_items(category: Dict[str, Any]) -> List[str]:
    return [
        str(item)
        for group in category.get("groups", [])
        if not group.get("spacer")
        for item in group.get("items", [])
    ]


def source_categories(data: Dict[str, Any]) -> List[Dict[str, Any]]:
    return [
        category
        for category in data.get("categories", [])
        if category.get("role") != "all"
    ]


def find_subsequence(haystack: Sequence[str], needle: Sequence[str]) -> int:
    if not needle or len(needle) > len(haystack):
        return -1
    first = needle[0]
    limit = len(haystack) - len(needle) + 1
    for start in range(limit):
        if haystack[start] == first and list(haystack[start : start + len(needle)]) == list(needle):
            return start
    return -1


def require_ordered_series(
    label: str,
    expected: Sequence[str],
    categories: Sequence[Dict[str, Any]],
    errors: List[str],
) -> None:
    for category in categories:
        if find_subsequence(category_items(category), expected) >= 0:
            return
    visible = {item for category in categories for item in category_items(category)}
    missing = [item for item in expected if item not in visible]
    if missing:
        errors.append(f"{label}: missing {len(missing)} items, first={missing[:8]!r}")
    else:
        errors.append(f"{label}: all items exist but are no longer in Unicode order")


def require_complete_series(
    label: str,
    expected: Sequence[str],
    categories: Sequence[Dict[str, Any]],
    errors: List[str],
) -> None:
    """Check that every symbol of a series is present, ignoring its order.

    Used for series whose on-screen arrangement is deliberately curated in
    catalog.txt rather than following code point order -- mahjong tiles are
    grouped by suit, tile/card backs are moved out of the middle of the
    run, and so on.  Enforcing Unicode order there would report a false
    failure for an intentional layout, so only completeness is checked:
    losing or duplicating a symbol is still caught (here and by the global
    duplicate checks), while rearranging one is allowed.
    """
    visible = {item for category in categories for item in category_items(category)}
    missing = [item for item in expected if item not in visible]
    if missing:
        errors.append(f"{label}: missing {len(missing)} items, first={missing[:8]!r}")


def regional_flag(text: str) -> bool:
    return len(text) == 2 and all(0x1F1E6 <= ord(ch) <= 0x1F1FF for ch in text)


def tag_flag(text: str) -> bool:
    return len(text) >= 3 and ord(text[0]) == 0x1F3F4 and ord(text[-1]) == 0xE007F


def variant_key(text: str) -> str:
    return "".join(
        char for char in str(text)
        if ord(char) != 0xFE0F and not (0x1F3FB <= ord(char) <= 0x1F3FF)
    )


def ordered_unique(items: Iterable[str]) -> List[str]:
    seen: Set[str] = set()
    output: List[str] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            output.append(item)
    return output


def main() -> int:
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    series = load_series()
    errors: List[str] = []
    categories = data.get("categories", [])
    source = source_categories(data)

    if not categories or categories[0].get("role") != "all":
        errors.append("generated all-symbol category must be category index 0 with role=all")

    # Structural checks: row/column metadata and category-local deduplication.
    for category_index, category in enumerate(source, 1):
        seen: Set[str] = set()
        expected_row = 0
        for group in category.get("groups", []):
            if group.get("spacer"):
                continue
            expected_row += 1
            items = [str(item) for item in group.get("items", [])]
            cols = int(group.get("cols", 0) or 0)
            row = int(group.get("row", 0) or 0)
            if not (1 <= cols <= 12):
                errors.append(f"category {category_index}: invalid cols={cols}")
            if len(items) > 12:
                errors.append(f"category {category_index}, row {row}: more than 12 items")
            if row != expected_row:
                errors.append(
                    f"category {category_index}: row numbering is not structural "
                    f"(expected {expected_row}, got {row})"
                )
            duplicates = [item for item in items if item in seen]
            if duplicates:
                errors.append(
                    f"category {category_index}: duplicate references remain, first={duplicates[:8]!r}"
                )
            seen.update(items)

    # Important Unicode series are found by content, not by category/group name.
    circled_21_50 = [chr(cp) for cp in range(0x3251, 0x3260)] + [
        chr(cp) for cp in range(0x32B1, 0x32C0)
    ]
    require_ordered_series("circled numbers 21-50", circled_21_50, source, errors)

    letter_series: List[str] = []
    for start, end in (
        (0x24B6, 0x24D0),
        (0x24D0, 0x24EA),
        (0x249C, 0x24B6),
        (0x1F110, 0x1F12A),
        (0x1F130, 0x1F14A),
        (0x1F150, 0x1F16A),
        (0x1F170, 0x1F18A),
    ):
        letter_series.extend(chr(cp) for cp in range(start, end))
    require_ordered_series("seven A-Z enclosed-letter series", letter_series, source, errors)

    # Game tiles and cards are laid out for the picker, not by code point:
    # mahjong is grouped by suit, tile/card backs are pulled out of the
    # middle of their runs.  Only completeness is enforced for these.
    for label, key in (
        ("mahjong", "mahjong"),
        ("domino", "domino"),
        ("playing cards", "playing_cards"),
    ):
        require_complete_series(label, series[key], source, errors)

    for label, key in (
        ("chess symbols", "chess_symbols"),
        ("Phaistos disc", "phaistos"),
        ("Anatolian hieroglyphs", "anatolian_hieroglyphs"),
        ("cuneiform", "cuneiform"),
        ("cuneiform numbers", "cuneiform_numbers"),
        ("Egyptian hieroglyphs and controls", "egyptian_hieroglyphs_and_controls"),
    ):
        require_ordered_series(label, series[key], source, errors)

    all_source_items = [item for category in source for item in category_items(category)]
    regional_items = ordered_unique(item for item in all_source_items if regional_flag(item))
    tag_items = ordered_unique(item for item in all_source_items if tag_flag(item))
    if len(regional_items) != 259:
        errors.append(f"regional flags: expected 259 unique sequences, got {len(regional_items)}")
    if len(tag_items) != 3:
        errors.append(f"subdivision flags: expected 3 unique sequences, got {len(tag_items)}")

    territories = json.loads(
        (ROOT / "data-source" / "territory_names_zh.json").read_text(encoding="utf-8")
    )
    pseudo_regions = {"XA", "XB", "QO", "EZ", "ZZ"}
    expected_codes = {
        code
        for code in territories
        if len(code) == 2 and code.isalpha() and code.isupper() and code not in pseudo_regions
    }
    actual_codes = {
        "".join(chr(ord("A") + ord(ch) - 0x1F1E6) for ch in flag)
        for flag in regional_items
    }
    if actual_codes != expected_codes:
        errors.append(
            "regional flag codes differ from territory table: "
            f"missing={sorted(expected_codes-actual_codes)[:8]!r}, "
            f"extra={sorted(actual_codes-expected_codes)[:8]!r}"
        )

    records = data.get("symbols", [])
    records_by_text = {str(record.get("text", "")): record for record in records}
    bad_flag_names = [
        flag
        for flag in regional_items + tag_items
        if "旗" not in str(records_by_text.get(flag, {}).get("name_zh", ""))
        or not records_by_text.get(flag, {}).get("emoji")
    ]
    if bad_flag_names:
        errors.append(f"flag records need Chinese flag names and Emoji flags: {bad_flag_names[:8]!r}")

    bad_names = [
        record.get("text")
        for record in records
        if not record.get("name_zh")
        or record.get("name_zh") == "未标注"
        or not record.get("name_en")
    ]
    if bad_names:
        errors.append(f"missing bilingual names: {bad_names[:12]!r} (total {len(bad_names)})")

    visible = set(all_source_items)
    visible_keys = {variant_key(item) for item in visible}
    unlocatable = [
        record.get("text")
        for record in records
        if record.get("text") not in visible
        and variant_key(str(record.get("text", ""))) not in visible_keys
    ]
    if unlocatable:
        errors.append(
            f"searchable symbols without a source location: {unlocatable[:8]!r} "
            f"(total {len(unlocatable)})"
        )

    # Generated all-symbol category must be the unique source-order union.
    if categories:
        expected_all = ordered_unique(all_source_items)
        actual_all = category_items(categories[0])
        if actual_all != expected_all:
            errors.append(
                f"generated all-symbol union differs: expected {len(expected_all)}, got {len(actual_all)}"
            )

    main_indices = [int(value) for value in data.get("ui_main_category_indices", [])]
    other_indices = [int(value) for value in data.get("ui_other_category_indices", [])]
    if len(main_indices) != len(set(main_indices)) or len(other_indices) != len(set(other_indices)):
        errors.append("UI category index lists contain duplicates")
    if set(main_indices).intersection(other_indices):
        errors.append("a category cannot be both main and other")
    for index in main_indices + other_indices:
        if not (1 <= index < len(categories)):
            errors.append(f"UI category index out of range: {index}")
    for index in main_indices:
        if categories[index].get("ui_section", "main") != "main":
            errors.append(f"UI main index {index} does not point to a main category")
    for index in other_indices:
        if categories[index].get("ui_section") != "other":
            errors.append(f"UI other index {index} does not point to an other category")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    egyptian_count = len(series["egyptian_hieroglyphs_and_controls"])
    print(
        "catalog audit passed: "
        f"{len(records)} symbol records, headings are display-only, "
        "259 regional flags + 3 subdivision flags, complete card/tile series, "
        f"{egyptian_count} Egyptian characters, all searchable records locatable"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
