#!/usr/bin/env python3
"""Convert YeSymbol catalog JSON to/from a human-editable Markdown-like text format.

The text file is the preferred hand-maintained source.  The generated JSON remains
an intermediate file consumed by the C data generator.

Commands:
  export [json] [txt]   Convert JSON to text.
  build  [txt] [json]  Convert text to normalized/deduplicated JSON.
  check  [txt]         Parse and validate the text without writing JSON.
  apply-cldr [txt]    Apply cached CLDR Chinese TTS names to #@symbols.
  dedupe [json] [json] Normalize/deduplicate an existing JSON file.
"""
from __future__ import annotations

import argparse
import copy
import csv
import json
import re
import sys
import unicodedata
from collections import OrderedDict, defaultdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, MutableMapping, Optional, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_JSON = ROOT / "data-source" / "catalog.generated.json"
DEFAULT_TXT = ROOT / "data-source" / "catalog.txt"
DEFAULT_REPORT = ROOT / "data-source" / "catalog-dedupe-report.txt"
FORMAT_ID = "yesymbol-catalog-text/2"
GENERATED_CATEGORY_NAME = "全部符号"
MAX_COLUMNS = 12
CLDR_CACHE = ROOT / "data-source" / "cldr-annotations-zh.tts.json"
_VARIATION_SELECTOR_16 = "\ufe0f"
UI_SECTION_MAIN = "main"
UI_SECTION_OTHER = "other"
UI_SECTION_HIDDEN = "hidden"
VALID_UI_SECTIONS = {UI_SECTION_MAIN, UI_SECTION_OTHER, UI_SECTION_HIDDEN}
GENERATED_SUPPLEMENT_ROLE = "generated-supplement"
COMMON_ROLE = "common"
MAX_COMMON = 256



def jdump(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def chunks(items: Sequence[str], size: int) -> Iterable[List[str]]:
    size = max(1, min(MAX_COLUMNS, int(size or MAX_COLUMNS)))
    for start in range(0, len(items), size):
        yield list(items[start : start + size])


def dedupe_sequence(items: Iterable[str]) -> Tuple[List[str], int]:
    seen = set()
    output: List[str] = []
    removed = 0
    for item in items:
        if item in seen:
            removed += 1
            continue
        seen.add(item)
        output.append(item)
    return output, removed


def blockify_category_groups(groups: Sequence[MutableMapping[str, Any]]) -> List[Dict[str, Any]]:
    """Convert row-level groups into logical titled blocks.

    The text parser stores the title only on the first physical row and leaves
    continuation rows untitled.  Treat those continuation rows as part of the
    same logical block so repeated ``##`` headings can be merged safely.
    """
    blocks: List[Dict[str, Any]] = []
    current: Optional[Dict[str, Any]] = None
    for raw_group in groups:
        group = copy.deepcopy(dict(raw_group))
        if group.get("spacer"):
            if current is not None:
                blocks.append(current)
                current = None
            blocks.append({"spacer": True})
            continue

        title = str(group.get("title", "")).strip()
        if title:
            if current is not None:
                blocks.append(current)
            current = {"spacer": False, "title": title, "rows": [group]}
        elif current is None:
            current = {"spacer": False, "title": "", "rows": [group]}
        else:
            current["rows"].append(group)

    if current is not None:
        blocks.append(current)
    return blocks


def normalize_block_spacers(blocks: Sequence[MutableMapping[str, Any]]) -> List[Dict[str, Any]]:
    output: List[Dict[str, Any]] = []
    for raw_block in blocks:
        block = copy.deepcopy(dict(raw_block))
        if block.get("spacer"):
            if not output or output[-1].get("spacer"):
                continue
            output.append({"spacer": True})
        else:
            output.append(block)
    while output and output[-1].get("spacer"):
        output.pop()
    return output


def flatten_category_blocks(blocks: Sequence[MutableMapping[str, Any]]) -> List[Dict[str, Any]]:
    groups: List[Dict[str, Any]] = []
    row_number = 0
    for block in normalize_block_spacers(blocks):
        if block.get("spacer"):
            groups.append({"title": "", "cols": 0, "row": 0, "spacer": True, "items": []})
            continue
        title = str(block.get("title", "")).strip()
        for row_index, raw_row in enumerate(block.get("rows", [])):
            items = [str(item) for item in raw_row.get("items", []) if str(item)]
            if not items:
                continue
            row_number += 1
            groups.append(
                {
                    "title": title if row_index == 0 else "",
                    "cols": max(1, min(MAX_COLUMNS, int(raw_row.get("cols", 0) or len(items)))),
                    "row": row_number,
                    "items": items,
                }
            )
    return groups


def merge_duplicate_group_blocks(
    groups: Sequence[MutableMapping[str, Any]],
) -> Tuple[List[Dict[str, Any]], int]:
    """Merge repeated exact ``##`` titles inside one first-level category.

    The first heading keeps its position.  Rows from later headings are
    appended to that block in source order.  Anonymous groups are not merged.
    """
    blocks = blockify_category_groups(groups)
    output: List[Dict[str, Any]] = []
    first_by_title: Dict[str, Dict[str, Any]] = {}
    merged_count = 0

    for raw_block in blocks:
        block = copy.deepcopy(raw_block)
        if block.get("spacer"):
            output.append(block)
            continue
        title = str(block.get("title", "")).strip()
        if title and title in first_by_title:
            first_by_title[title]["rows"].extend(block.get("rows", []))
            merged_count += 1
            continue
        output.append(block)
        if title:
            first_by_title[title] = block

    return flatten_category_blocks(output), merged_count


def merge_duplicate_categories(
    categories: Sequence[MutableMapping[str, Any]],
) -> Tuple[List[Dict[str, Any]], int]:
    """Merge repeated first-level headings without interpreting their text.

    The first ``#`` heading keeps its position, description and UI section.
    Later categories with the same display name append their rows in source
    order.  This is a purely structural operation: no Chinese or English title
    has special meaning.
    """
    output: List[Dict[str, Any]] = []
    first_by_name: Dict[str, Dict[str, Any]] = {}
    merged = 0
    for raw in categories:
        category = copy.deepcopy(dict(raw))
        name = str(category.get("name", ""))
        first = first_by_name.get(name)
        if first is None:
            output.append(category)
            first_by_name[name] = category
            continue
        if not str(first.get("desc", "")).strip() and str(category.get("desc", "")).strip():
            first["desc"] = category.get("desc", "")
        first_groups = list(first.get("groups", []))
        later_groups = list(category.get("groups", []))
        if first_groups and later_groups and not first_groups[-1].get("spacer"):
            first_groups.append({"title": "", "cols": 0, "row": 0, "spacer": True, "items": []})
        first_groups.extend(later_groups)
        first["groups"] = first_groups
        merged += 1
    return output, merged


def group_priority(group: MutableMapping[str, Any], index: int) -> Tuple[int, int]:
    # Named curated groups are more useful than anonymous raw rows.  Among groups
    # of the same kind, earlier source order wins.
    named_penalty = 0 if str(group.get("title", "")).strip() else 1
    return named_penalty, index


def dedupe_category_groups(groups: Sequence[MutableMapping[str, Any]]) -> Tuple[List[Dict[str, Any]], int, int]:
    """Remove duplicate references within one category without losing symbols.

    A symbol may still appear in multiple *different* categories.  Within one
    category, a named group is preferred over an anonymous group; otherwise the
    first group wins.  Spacer groups are preserved.
    """
    normalized: List[Dict[str, Any]] = []
    local_removed = 0
    category_removed = 0

    for group in groups:
        g = {
            "title": str(group.get("title", "")),
            "cols": max(0, min(MAX_COLUMNS, int(group.get("cols", 0) or 0))),
            "row": max(0, int(group.get("row", 0) or 0)),
            "items": list(group.get("items", [])),
        }
        if group.get("spacer"):
            g["spacer"] = True
            g["items"] = []
            g["cols"] = 0
            g["row"] = 0
        else:
            g["items"], removed = dedupe_sequence(str(x) for x in g["items"] if str(x))
            local_removed += removed
        normalized.append(g)

    occurrences: Dict[str, List[int]] = defaultdict(list)
    for gi, group in enumerate(normalized):
        if group.get("spacer"):
            continue
        for symbol in group["items"]:
            occurrences[symbol].append(gi)

    keeper: Dict[str, int] = {}
    for symbol, indices in occurrences.items():
        keeper[symbol] = min(indices, key=lambda gi: group_priority(normalized[gi], gi))

    for gi, group in enumerate(normalized):
        if group.get("spacer"):
            continue
        kept: List[str] = []
        for symbol in group["items"]:
            if keeper[symbol] == gi:
                kept.append(symbol)
            else:
                category_removed += 1
        group["items"] = kept

    # Keep explicit spacers, but remove empty normal groups created solely by
    # deduplication.  This avoids blank titled blocks in the UI.
    normalized = [g for g in normalized if g.get("spacer") or g.get("items")]
    return normalized, local_removed, category_removed


def normalize_cldr_key(text: str) -> str:
    """Match the pinned CLDR annotations, which omit U+FE0F in cp values."""
    return str(text).replace(_VARIATION_SELECTOR_16, "")


def normalize_visible_variant_key(text: str) -> str:
    """Collapse presentation selector and skin-tone-only variants.

    Such records remain searchable and available to the Emoji context menu, but
    they do not need a second visible catalog cell when their base sequence is
    already placed.
    """
    return "".join(
        char for char in str(text)
        if char != _VARIATION_SELECTOR_16 and not (0x1F3FB <= ord(char) <= 0x1F3FF)
    )


def load_cldr_tts(path: Path = CLDR_CACHE) -> Dict[str, str]:
    if not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    source = payload.get("tts", payload) if isinstance(payload, dict) else {}
    if not isinstance(source, dict):
        return {}
    return {
        normalize_cldr_key(str(key)): str(value).strip()
        for key, value in source.items()
        if str(key) and str(value).strip()
    }


_LOW_QUALITY_ENGLISH = re.compile(
    r"(?:^|[·、，, /])(?:TAG|LATIN|CJK|UNICODE|EMOJI|WAVING|FLAG|SQUARE|LETTER|SIGN|SYMBOL|WITH|AND|OF|THE|SMALL|CAPITAL|BLACK|WHITE|NUMBER|DIGIT|CARD|TILE|HIEROGLYPH|CUNEIFORM)(?:$|[·、，, /])",
    re.IGNORECASE,
)


def should_use_cldr_name(record: MutableMapping[str, Any], cldr_name: str) -> bool:
    """Use CLDR for Emoji and for obviously unfinished mixed-language labels.

    Deliberate project names such as “金牛座” or “带圈数字二十一” remain
    untouched unless the record is an Emoji.  This lets CLDR repair machine-like
    labels without erasing useful hand curation.
    """
    current = str(record.get("name_zh", "")).strip()
    if not cldr_name or cldr_name == current:
        return False
    if bool(record.get("emoji", False)):
        return True
    if not current or current == "未标注" or current.startswith("U+"):
        return True
    if _LOW_QUALITY_ENGLISH.search(current):
        return True
    # Mixed Chinese/English fragments are a strong sign that the earlier name
    # was generated word-by-word.  Exclude simple one-letter educational labels
    # such as “拼音 a 一声”.
    latin_words = re.findall(r"[A-Za-z]{2,}", current)
    return bool(latin_words)


def apply_cldr_tts(records: Sequence[MutableMapping[str, Any]], path: Path = CLDR_CACHE) -> int:
    names = load_cldr_tts(path)
    if not names:
        return 0
    changed = 0
    for record in records:
        cldr_name = names.get(normalize_cldr_key(str(record.get("text", ""))))
        if cldr_name and should_use_cldr_name(record, cldr_name):
            record["name_zh"] = cldr_name
            changed += 1
    return changed


def normalize_symbol_records(records: Sequence[MutableMapping[str, Any]]) -> Tuple[List[Dict[str, Any]], int]:
    by_text: "OrderedDict[str, Dict[str, Any]]" = OrderedDict()
    duplicates = 0
    for raw in records:
        text = str(raw.get("text", ""))
        if not text:
            continue
        name_en = str(raw.get("name_en") or raw.get("name") or fallback_english_name(text))
        name_zh = str(raw.get("name_zh") or "未标注")
        record = {
            "text": text,
            "name": name_en,
            "emoji": bool(raw.get("emoji", False)),
            "name_en": name_en,
            "name_zh": name_zh,
        }
        if text not in by_text:
            by_text[text] = record
        else:
            duplicates += 1
            existing = by_text[text]
            # Later hand edits fill blanks, but do not silently overwrite a
            # non-empty earlier annotation.
            if existing["name_zh"] in ("", "未标注") and name_zh not in ("", "未标注"):
                existing["name_zh"] = name_zh
            if not existing["name_en"] and name_en:
                existing["name_en"] = existing["name"] = name_en
            existing["emoji"] = bool(existing["emoji"] or record["emoji"])
    return list(by_text.values()), duplicates


def fallback_english_name(text: str) -> str:
    names: List[str] = []
    for char in text:
        try:
            names.append(unicodedata.name(char))
        except ValueError:
            names.append(f"U+{ord(char):04X}")
    return " / ".join(names) if names else "UNNAMED SYMBOL"


def build_all_category(categories: Sequence[MutableMapping[str, Any]]) -> Dict[str, Any]:
    seen = set()
    groups: List[Dict[str, Any]] = []
    for category in categories:
        items: List[str] = []
        for group in category.get("groups", []):
            if group.get("spacer"):
                continue
            for symbol in group.get("items", []):
                if symbol not in seen:
                    seen.add(symbol)
                    items.append(symbol)
        if items:
            groups.append(
                {
                    "title": str(category.get("name", "")),
                    "cols": MAX_COLUMNS,
                    "row": 0,
                    "items": items,
                }
            )
    return {
        "name": GENERATED_CATEGORY_NAME,
        "desc": "",
        "groups": groups,
        "ui_section": "runtime",
        "role": "all",
    }


def normalize_catalog(data: MutableMapping[str, Any]) -> Tuple[Dict[str, Any], Dict[str, int]]:
    stats = {
        "symbol_definition_duplicates": 0,
        "within_group_duplicates": 0,
        "within_category_duplicates": 0,
        "duplicate_group_headings_merged": 0,
        "duplicate_categories_merged": 0,
        "supplement_groups_reclassified": 0,
        "unreferenced_symbol_definitions_removed": 0,
        "missing_symbol_definitions_created": 0,
        "cldr_name_replacements": 0,
    }

    original_all_items: List[str] = []
    for raw_category in data.get("categories", []):
        if str(raw_category.get("name", "")) == GENERATED_CATEGORY_NAME:
            for raw_group in raw_category.get("groups", []):
                if not raw_group.get("spacer"):
                    original_all_items.extend(str(x) for x in raw_group.get("items", []) if str(x))

    input_categories = [
        copy.deepcopy(c)
        for c in data.get("categories", [])
        if str(c.get("name", "")) != GENERATED_CATEGORY_NAME
    ]
    # Backward compatibility for exporting or deduplicating pre-v1.1 JSON:
    # turn its detached default_common list into the same ordinary category
    # shape now used by catalog.txt. New text sources never need this path.
    if not any(str(category.get("role", "")) == COMMON_ROLE for category in input_categories):
        legacy_common = [str(item) for item in data.get("default_common", []) if str(item)]
        if legacy_common:
            input_categories.insert(
                0,
                {
                    "name": "常用符号",
                    "desc": "",
                    "groups": [
                        {"title": "", "cols": len(row), "row": index + 1, "items": row}
                        for index, row in enumerate(chunks(legacy_common, MAX_COLUMNS))
                    ],
                    "ui_section": UI_SECTION_MAIN,
                    "role": COMMON_ROLE,
                },
            )
    # Category and group display names are user-editable data.  Never move
    # blocks by matching Chinese/English titles: a renamed heading must not
    # change its meaning or make regeneration fail.
    stats["supplement_groups_reclassified"] = 0

    categories: List[Dict[str, Any]] = []
    for raw_category in input_categories:
        merged_groups, merged_headings = merge_duplicate_group_blocks(raw_category.get("groups", []))
        stats["duplicate_group_headings_merged"] += merged_headings
        groups, local_removed, category_removed = dedupe_category_groups(merged_groups)
        stats["within_group_duplicates"] += local_removed
        stats["within_category_duplicates"] += category_removed
        categories.append(
            {
                "name": str(raw_category.get("name", "")),
                "desc": str(raw_category.get("desc", "")),
                "groups": groups,
                "ui_section": str(raw_category.get("ui_section", UI_SECTION_MAIN)),
                "role": str(raw_category.get("role", "")),
            }
        )

    categories, merged_categories = merge_duplicate_categories(categories)
    stats["duplicate_categories_merged"] = merged_categories
    # Merging categories can create repeated ``##`` titles across the newly
    # joined blocks.  Normalize each merged category once more structurally.
    for category_item in categories:
        merged_groups, merged_headings = merge_duplicate_group_blocks(category_item.get("groups", []))
        stats["duplicate_group_headings_merged"] += merged_headings
        groups, local_removed, category_removed = dedupe_category_groups(merged_groups)
        category_item["groups"] = flatten_category_blocks(blockify_category_groups(groups))
        stats["within_group_duplicates"] += local_removed
        stats["within_category_duplicates"] += category_removed

    records, definition_duplicates = normalize_symbol_records(data.get("symbols", []))
    stats["symbol_definition_duplicates"] = definition_duplicates
    stats["cldr_name_replacements"] = apply_cldr_tts(records)
    by_text: "OrderedDict[str, Dict[str, Any]]" = OrderedDict((r["text"], r) for r in records)

    # Every ordinary built-in symbol must have a real catalog location so a
    # search result can report its source and the context menu can jump there.
    # Keep presentation/skin-tone variants hidden when their base sequence is
    # already visible, but place otherwise orphaned non-Emoji records into a
    # generated “补充符号 → 未分类补充” block.  This also makes “全部符号” a
    # genuine union of every non-variant built-in record.
    visible_texts = {
        symbol
        for category in categories
        for group in category["groups"]
        for symbol in group.get("items", [])
    }
    visible_variant_keys = {normalize_visible_variant_key(symbol) for symbol in visible_texts}
    orphan_symbols = [
        text for text, record in by_text.items()
        if text not in visible_texts
        and not bool(record.get("emoji", False))
        and normalize_visible_variant_key(text) not in visible_variant_keys
    ]
    if orphan_symbols:
        # Generated orphan placement is intentionally independent of any
        # user-facing ``#`` or ``##`` title.  The synthetic category is hidden
        # from the sidebar and is not exported back into catalog.txt.
        supplement = next(
            (category for category in categories if category.get("role") == GENERATED_SUPPLEMENT_ROLE),
            None,
        )
        if supplement is None:
            supplement = {
                "name": "未分类补充",
                "desc": "由构建工具自动收纳未在正文中摆放的字符。",
                "groups": [],
                "ui_section": UI_SECTION_HIDDEN,
                "role": GENERATED_SUPPLEMENT_ROLE,
            }
            categories.append(supplement)
        supplement["groups"] = []
        for row_offset, row_items in enumerate(chunks(orphan_symbols, MAX_COLUMNS)):
            supplement["groups"].append(
                {
                    "title": "未分类补充" if row_offset == 0 else "",
                    "items": list(row_items),
                    "cols": len(row_items),
                    "row": row_offset + 1,
                }
            )
        stats["orphan_symbols_placed"] = len(orphan_symbols)
    else:
        stats["orphan_symbols_placed"] = 0

    referenced_order: List[str] = []
    referenced_seen = set()
    for category in categories:
        for group in category["groups"]:
            for symbol in group.get("items", []):
                if symbol not in referenced_seen:
                    referenced_seen.add(symbol)
                    referenced_order.append(symbol)
    for symbol in data.get("default_common", []):
        symbol = str(symbol)
        if symbol and symbol not in referenced_seen:
            referenced_seen.add(symbol)
            referenced_order.append(symbol)

    # Keep annotations that are not shown in a visible category.  This is
    # intentional for Emoji skin-tone/ZWJ variants: they must remain compiled
    # into the executable for search and variant lookup, but should not flood
    # the visible “补充符号” category.  The text source therefore controls only
    # visible placement, while the #@symbols table may contain hidden records.
    for symbol in list(original_all_items) + list(by_text.keys()):
        if symbol and symbol not in referenced_seen:
            referenced_seen.add(symbol)
            referenced_order.append(symbol)

    for symbol in referenced_order:
        if symbol not in by_text:
            by_text[symbol] = {
                "text": symbol,
                "name": fallback_english_name(symbol),
                "emoji": False,
                "name_en": fallback_english_name(symbol),
                "name_zh": "未标注",
            }
            stats["missing_symbol_definitions_created"] += 1

    pruned_records = [by_text[symbol] for symbol in referenced_order]
    stats["unreferenced_symbol_definitions_removed"] = 0

    common_categories = [category for category in categories if category.get("role") == COMMON_ROLE]
    default_common, default_removed = dedupe_sequence(
        str(symbol)
        for category in common_categories
        for group in category.get("groups", [])
        if not group.get("spacer")
        for symbol in group.get("items", [])
        if str(symbol)
    )
    stats["default_common_duplicates"] = default_removed

    all_category = build_all_category(categories)
    output_categories = [all_category] + categories
    main_indices = [
        index + 1 for index, category_item in enumerate(categories)
        if category_item.get("ui_section", UI_SECTION_MAIN) == UI_SECTION_MAIN
        and category_item.get("role") not in (GENERATED_SUPPLEMENT_ROLE, COMMON_ROLE)
    ]
    other_indices = [
        index + 1 for index, category_item in enumerate(categories)
        if category_item.get("ui_section", UI_SECTION_MAIN) == UI_SECTION_OTHER
        and category_item.get("role") not in (GENERATED_SUPPLEMENT_ROLE, COMMON_ROLE)
    ]
    main_names = [output_categories[index]["name"] for index in main_indices]
    other_names = [output_categories[index]["name"] for index in other_indices]
    output: Dict[str, Any] = {
        "categories": output_categories,
        "symbols": pruned_records,
        "default_common": default_common,
        # Visible order is structural: first-level headings keep source order.
        # Their display text may be renamed freely.  ``@ui-section other``
        # controls the collapsible subsection; ``hidden`` remains searchable.
        "ui_categories": ["常用符号"]
        + main_names
        + (["其他符号"] + other_names if other_names else [])
        + [GENERATED_CATEGORY_NAME, "自定义"],
        "ui_category_groups": {"其他符号": other_names} if other_names else {},
        "ui_main_category_indices": main_indices,
        "ui_other_category_indices": other_indices,
        "name_languages": list(data.get("name_languages", ["zh-CN", "en"])),
        "name_note": str(data.get("name_note", "")),
        "curation_note": str(data.get("curation_note", "")),
    }
    return output, stats


def encode_field(value: Any, delimiter: str = ",") -> str:
    """Encode one human-editable field, quoting only when ambiguity requires it."""
    text = str(value)
    needs_quotes = (
        text == ""
        or delimiter in text
        or '"' in text
        or "\n" in text
        or "\r" in text
        or text != text.strip()
        or text == "---"
        or text.startswith("# ")
        or text.startswith("## ")
        or text.startswith("<!--")
    )
    if not needs_quotes:
        return text
    return '"' + text.replace('"', '""') + '"'


def encode_delimited(values: Sequence[Any], delimiter: str = ",") -> str:
    return delimiter.join(encode_field(value, delimiter) for value in values)


def parse_delimited(value_text: str, delimiter: str, line_no: int) -> List[str]:
    try:
        reader = csv.reader(
            [value_text],
            delimiter=delimiter,
            quotechar='"',
            doublequote=True,
            skipinitialspace=True,
            strict=True,
        )
        values = next(reader)
    except (csv.Error, StopIteration) as exc:
        raise ValueError(f"line {line_no}: invalid delimited row: {exc}") from exc
    return [str(value) for value in values]


def parse_plain_or_json(value_text: str) -> Any:
    value_text = value_text.strip()
    if not value_text:
        return ""
    if value_text[0] in '[{"' or value_text in ('true', 'false', 'null'):
        try:
            return json.loads(value_text)
        except json.JSONDecodeError:
            pass
    return value_text


def export_text(data: MutableMapping[str, Any], destination: Path) -> None:
    data, _ = normalize_catalog(copy.deepcopy(data))
    lines: List[str] = []
    lines.append(f"@format {FORMAT_ID}")
    lines.append("@name-languages " + encode_delimited(data.get("name_languages", ["zh-CN", "en"])))
    lines.append("@name-note " + str(data.get("name_note", "")).replace("\n", " "))
    lines.append("@curation-note " + str(data.get("curation_note", "")).replace("\n", " "))
    lines.append("")
    lines.append("<!-- 全部符号由转换工具自动生成，不在此文件重复维护。 -->")
    lines.append("<!-- 一级分类直接由正文中的 # 标题生成，不需要维护分类名单。 -->")
    lines.append("<!-- 普通符号直接用逗号分隔；只有空格、逗号等特殊项目才需要双引号。 -->")
    lines.append("<!-- #/##/@desc 的显示文字可以自由修改；@ui-section 只控制侧边栏区域。 -->")
    lines.append("")

    active_section = UI_SECTION_MAIN
    for category in data.get("categories", []):
        if category.get("role") in ("all", GENERATED_SUPPLEMENT_ROLE):
            continue
        section = str(category.get("ui_section", UI_SECTION_MAIN))
        if section not in VALID_UI_SECTIONS:
            section = UI_SECTION_MAIN
        if section != active_section:
            lines.append(f"@ui-section {section}")
            lines.append("")
            active_section = section
        lines.append(f"# {category.get('name', '')}")
        role = str(category.get("role", ""))
        if role == COMMON_ROLE:
            lines.append("@role common")
        description = str(category.get("desc", "")).replace("\n", " ")
        if description:
            lines.append(f"@desc {description}")
        lines.append("")
        for group in category.get("groups", []):
            if group.get("spacer"):
                if lines and lines[-1] != "":
                    lines.append("")
                lines.append("---")
                lines.append("")
                continue
            title = str(group.get("title", "")).strip()
            if title:
                lines.append(f"## {title}")
            item_list = list(group.get("items", []))
            if item_list:
                width = max(1, min(MAX_COLUMNS, int(group.get("cols", 0) or MAX_COLUMNS)))
                for row in chunks(item_list, width):
                    lines.append(encode_delimited(row, ","))
            # Anonymous continuation rows stay adjacent to the preceding titled
            # block; no artificial ``## _`` heading is emitted.
            if title:
                lines.append("")

    lines.append("#@symbols")
    lines.append("<!-- 格式：@symbol 符号|中文名称|英文名称|emoji/false -->")
    for record in data.get("symbols", []):
        lines.append(
            "@symbol "
            + encode_delimited(
                [
                    record.get("text", ""),
                    record.get("name_zh", ""),
                    record.get("name_en") or record.get("name", ""),
                    "emoji" if bool(record.get("emoji", False)) else "false",
                ],
                "|",
            )
        )
    lines.append("")
    destination.write_text("\n".join(lines), encoding="utf-8", newline="\n")

def parse_json_value(line: str, directive: str, line_no: int) -> Any:
    value_text = line[len(directive) :].strip()
    try:
        return json.loads(value_text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"line {line_no}: invalid JSON after {directive}: {exc}") from exc


def parse_text(source: Path) -> Dict[str, Any]:
    lines = source.read_text(encoding="utf-8-sig").splitlines()
    data: Dict[str, Any] = {
        "categories": [],
        "symbols": [],
        "default_common": [],
        "ui_categories": [],
        "name_languages": ["zh-CN", "en"],
        "name_note": "",
        "curation_note": "",
    }
    current_category: Optional[Dict[str, Any]] = None
    pending_group_title: Optional[str] = None
    pending_group_used = False
    in_symbols = False
    format_seen = False
    category_row = 0
    legacy_cols = MAX_COLUMNS
    legacy_row = 0
    current_ui_section = UI_SECTION_MAIN

    def require_category(line_no: int) -> Dict[str, Any]:
        if current_category is None:
            raise ValueError(f"line {line_no}: group/content appears before a category")
        return current_category

    def append_symbol_row(items: Sequence[str], line_no: int) -> None:
        nonlocal pending_group_title, pending_group_used, category_row
        category = require_category(line_no)

        # A first-level category may contain rows directly.  Requiring an
        # artificial ``## _`` before the first row defeats the purpose of the
        # hand-editable text format, so create an anonymous group implicitly.
        if pending_group_title is None:
            pending_group_title = ""
            pending_group_used = False

        clean = [str(item) for item in items if str(item) != ""]
        if not clean:
            raise ValueError(f"line {line_no}: symbol row is empty")

        # The UI has a twelve-column limit.  Long human-written lines such as
        # A-Z are accepted and wrapped automatically instead of forcing the
        # editor to count and split every row by hand.
        for part in chunks(clean, MAX_COLUMNS):
            category_row += 1
            category["groups"].append(
                {
                    "title": pending_group_title if not pending_group_used else "",
                    "cols": len(part),
                    "row": category_row,
                    "items": part,
                }
            )
            pending_group_used = True

    for line_no, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("<!--"):
            # Backward-compatible structural markers used by older catalogs.
            # The visible wording is not otherwise interpreted.
            if "折叠到" in line and "其他符号" in line:
                current_ui_section = UI_SECTION_OTHER
            elif "不单独显示" in line or "ui-section:hidden" in line:
                current_ui_section = UI_SECTION_HIDDEN
            continue
        if line == "#@symbols":
            in_symbols = True
            current_category = None
            pending_group_title = None
            continue
        if in_symbols:
            if not line.startswith("@symbol "):
                raise ValueError(f"line {line_no}: only @symbol records are allowed after #@symbols")
            value_text = line[len("@symbol ") :]
            if value_text.lstrip().startswith('["'):
                value = parse_json_value(line, "@symbol", line_no)
            else:
                value = parse_delimited(value_text, "|", line_no)
            if not isinstance(value, list) or len(value) != 4:
                raise ValueError(f"line {line_no}: @symbol must contain text, name_zh, name_en and emoji")
            text, name_zh, name_en, emoji = value
            emoji_value = bool(emoji) if isinstance(emoji, bool) else str(emoji).strip().lower() in ("1", "true", "yes", "emoji")
            data["symbols"].append(
                {
                    "text": str(text),
                    "name": str(name_en),
                    "emoji": emoji_value,
                    "name_en": str(name_en),
                    "name_zh": str(name_zh),
                }
            )
            continue

        if line.startswith("@format "):
            raw_value = line[len("@format ") :].strip()
            value = parse_plain_or_json(raw_value)
            if value not in (FORMAT_ID, "yesymbol-catalog-text/1"):
                raise ValueError(f"line {line_no}: unsupported catalog format {value!r}")
            format_seen = True
            continue
        if line.startswith("@name-languages "):
            value_text = line[len("@name-languages ") :].strip()
            value = parse_plain_or_json(value_text)
            data["name_languages"] = list(value) if isinstance(value, list) else parse_delimited(value_text, ",", line_no)
            continue
        if line.startswith("@name-note "):
            value_text = line[len("@name-note ") :].strip()
            data["name_note"] = str(parse_plain_or_json(value_text))
            continue
        if line.startswith("@curation-note "):
            value_text = line[len("@curation-note ") :].strip()
            data["curation_note"] = str(parse_plain_or_json(value_text))
            continue
        if line.startswith("@ui-section "):
            section = line[len("@ui-section ") :].strip().lower()
            if section not in VALID_UI_SECTIONS:
                raise ValueError(
                    f"line {line_no}: @ui-section must be one of main, other or hidden"
                )
            current_ui_section = section
            continue
        if line.startswith("@ui-categories "):
            # Legacy metadata only.  Visible categories are always derived from
            # the first-level headings below, so this list cannot become stale.
            continue
        if line.startswith("@role "):
            category = require_category(line_no)
            role = line[len("@role ") :].strip().lower()
            if role != COMMON_ROLE:
                raise ValueError(f"line {line_no}: @role must be common")
            if category.get("role"):
                raise ValueError(f"line {line_no}: category already has a role")
            category["role"] = role
            continue
        if line.startswith("##"):
            require_category(line_no)
            title = line[2:].strip()
            if not title:
                raise ValueError(f"line {line_no}: group name cannot be empty; use ## _ for an anonymous group")
            pending_group_title = "" if title == "_" else title
            pending_group_used = False
            legacy_cols = MAX_COLUMNS
            legacy_row = 0
            continue
        if line.startswith("#"):
            name = line[1:].strip()
            if not name:
                raise ValueError(f"line {line_no}: category name cannot be empty")
            current_category = {
                "name": name,
                "desc": "",
                "groups": [],
                "ui_section": current_ui_section,
                "role": "",
            }
            data["categories"].append(current_category)
            pending_group_title = None
            pending_group_used = False
            category_row = 0
            legacy_row = 0
            continue
        if line == "---":
            category = require_category(line_no)
            category["groups"].append({"title": "", "cols": 0, "row": 0, "spacer": True, "items": []})
            pending_group_title = None
            pending_group_used = False
            continue
        if line.startswith("@desc "):
            category = require_category(line_no)
            value_text = line[len("@desc ") :].strip()
            category["desc"] = str(parse_plain_or_json(value_text))
            continue
        # Legacy v1 directives are accepted for migration but are never exported.
        if line.startswith("@cols "):
            try:
                legacy_cols = max(1, min(MAX_COLUMNS, int(line[len("@cols") :].strip())))
            except ValueError as exc:
                raise ValueError(f"line {line_no}: @cols must be an integer") from exc
            continue
        if line.startswith("@row "):
            try:
                legacy_row = max(0, int(line[len("@row") :].strip()))
            except ValueError as exc:
                raise ValueError(f"line {line_no}: @row must be an integer") from exc
            continue
        if line.startswith("- ["):
            value = parse_json_value(line, "-", line_no)
            if not isinstance(value, list):
                raise ValueError(f"line {line_no}: legacy symbol row must be a JSON array")
            for row in chunks([str(item) for item in value if str(item)], legacy_cols):
                append_symbol_row(row, line_no)
            continue

        # v2 symbol row: comma-separated, with quotes only for fields that need them.
        append_symbol_row(parse_delimited(line, ",", line_no), line_no)

    if not format_seen:
        raise ValueError("missing @format directive")
    if not data["categories"]:
        raise ValueError("catalog has no categories")
    if not data["symbols"]:
        raise ValueError("catalog has no @symbol annotations")
    return data

def validate_catalog(data: MutableMapping[str, Any]) -> List[str]:
    errors: List[str] = []
    category_names = [str(c.get("name", "")) for c in data.get("categories", [])]
    if not category_names:
        errors.append("no categories")
    if len(category_names) != len(set(category_names)):
        errors.append("duplicate category names")

    common_categories = [category for category in data.get("categories", []) if category.get("role") == COMMON_ROLE]
    if len(common_categories) != 1:
        errors.append("catalog must contain exactly one @role common category")
    elif sum(
        len(group.get("items", []))
        for group in common_categories[0].get("groups", [])
        if not group.get("spacer")
    ) > MAX_COMMON:
        errors.append(f"common category exceeds {MAX_COMMON} items")

    valid_roles = {"", "all", COMMON_ROLE, GENERATED_SUPPLEMENT_ROLE}
    for category in data.get("categories", []):
        role = str(category.get("role", ""))
        if role not in valid_roles:
            errors.append(f"unsupported category role {role!r}")

    symbol_texts = [str(s.get("text", "")) for s in data.get("symbols", [])]
    if len(symbol_texts) != len(set(symbol_texts)):
        errors.append("duplicate symbol definitions")
    symbol_set = set(symbol_texts)
    for category in data.get("categories", []):
        for group in category.get("groups", []):
            if int(group.get("cols", 0) or 0) > MAX_COLUMNS:
                errors.append(f"{category.get('name')}/{group.get('title')}: cols exceeds {MAX_COLUMNS}")
            for symbol in group.get("items", []):
                if symbol not in symbol_set:
                    errors.append(f"missing definition for {symbol!r}")
    return errors


def write_report(path: Path, stats: MutableMapping[str, int], output: MutableMapping[str, Any]) -> None:
    lines = [
        "YeSymbol catalog normalization report",
        "=====================================",
        f"categories (including generated 全部符号): {len(output.get('categories', []))}",
        f"unique symbol definitions: {len(output.get('symbols', []))}",
        f"symbol definition duplicates removed: {stats.get('symbol_definition_duplicates', 0)}",
        f"duplicates removed inside the same group: {stats.get('within_group_duplicates', 0)}",
        f"duplicate references removed inside the same category: {stats.get('within_category_duplicates', 0)}",
        f"duplicate first-level headings merged: {stats.get('duplicate_categories_merged', 0)}",
        f"duplicate second-level headings merged: {stats.get('duplicate_group_headings_merged', 0)}",
        f"title-based supplement reclassification: disabled ({stats.get('supplement_groups_reclassified', 0)})",
        f"unreferenced definitions removed: {stats.get('unreferenced_symbol_definitions_removed', 0)}",
        f"missing definitions created: {stats.get('missing_symbol_definitions_created', 0)}",
        f"Chinese names replaced from pinned CLDR TTS cache: {stats.get('cldr_name_replacements', 0)}",
        f"default-common duplicates removed: {stats.get('default_common_duplicates', 0)}",
        f"unreferenced non-Emoji records placed in generated supplement: {stats.get('orphan_symbols_placed', 0)}",
        "",
        "Deduplication boundary:",
        "- exact duplicates inside one group are removed;",
        "- repeated references inside one category are removed; named groups win over unnamed rows;",
        "- the same symbol may remain in different categories intentionally;",
        "- unreferenced non-Emoji #@symbols records are placed in generated 补充符号/未分类补充 rows;",
        "- Emoji presentation and skin-tone variants may remain hidden and inherit the base symbol location;",
        "- 全部符号 is regenerated as one unique union and is not maintained in catalog.txt.",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def command_export(json_path: Path, txt_path: Path) -> int:
    data = json.loads(json_path.read_text(encoding="utf-8"))
    export_text(data, txt_path)
    print(f"exported {json_path} -> {txt_path}")
    return 0


def command_build(txt_path: Path, json_path: Path) -> int:
    parsed = parse_text(txt_path)
    output, stats = normalize_catalog(parsed)
    errors = validate_catalog(output)
    if errors:
        for error in errors[:50]:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    json_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_report(DEFAULT_REPORT, stats, output)
    print(
        f"built {json_path}: {len(output['symbols'])} unique symbols, "
        f"removed {stats['within_group_duplicates']} in-group and "
        f"{stats['within_category_duplicates']} in-category duplicate references, "
        f"merged {stats.get('duplicate_categories_merged', 0)} duplicate category headings and "
        f"{stats.get('duplicate_group_headings_merged', 0)} duplicate group headings, "
        f"placed {stats.get('orphan_symbols_placed', 0)} unreferenced non-Emoji records in generated supplement"
    )
    return 0


def command_check(txt_path: Path) -> int:
    parsed = parse_text(txt_path)
    output, stats = normalize_catalog(parsed)
    errors = validate_catalog(output)
    if errors:
        for error in errors[:50]:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(
        f"catalog OK: {len(output['categories'])} categories, {len(output['symbols'])} unique symbols; "
        f"would remove {stats['within_group_duplicates']} in-group and "
        f"{stats['within_category_duplicates']} in-category duplicate references; "
        f"would merge {stats.get('duplicate_categories_merged', 0)} duplicate category headings and "
        f"{stats.get('duplicate_group_headings_merged', 0)} duplicate group headings; "
        f"generated supplement would place {stats.get('orphan_symbols_placed', 0)} unreferenced non-Emoji records"
    )
    return 0


def command_apply_cldr(txt_path: Path) -> int:
    """Update only #@symbols Chinese fields while preserving hand layout."""
    names = load_cldr_tts()
    if not names:
        print(f"ERROR: CLDR cache is missing or empty: {CLDR_CACHE}", file=sys.stderr)
        return 1
    lines = txt_path.read_text(encoding="utf-8").splitlines()
    output: List[str] = []
    changed = 0
    in_symbols = False
    for line_no, raw in enumerate(lines, 1):
        stripped = raw.strip()
        if stripped == "#@symbols":
            in_symbols = True
            output.append(raw)
            continue
        if in_symbols and stripped.startswith("@symbol "):
            fields = parse_delimited(stripped[len("@symbol ") :], "|", line_no)
            if len(fields) != 4:
                raise ValueError(f"line {line_no}: @symbol expects four pipe-delimited fields")
            text, name_zh, name_en, emoji_text = fields
            record = {"text": text, "name_zh": name_zh, "name_en": name_en, "emoji": emoji_text.lower() in ("emoji", "true", "1")}
            cldr_name = names.get(normalize_cldr_key(text))
            if cldr_name and should_use_cldr_name(record, cldr_name):
                fields[1] = cldr_name
                changed += 1
                raw = "@symbol " + encode_delimited(fields, "|")
        output.append(raw)
    txt_path.write_text("\n".join(output) + "\n", encoding="utf-8", newline="\n")
    print(f"applied {changed} pinned CLDR Chinese TTS names to {txt_path}")
    return 0


def command_dedupe(input_path: Path, output_path: Path) -> int:
    data = json.loads(input_path.read_text(encoding="utf-8"))
    output, stats = normalize_catalog(data)
    output_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    write_report(DEFAULT_REPORT, stats, output)
    print(f"deduplicated {input_path} -> {output_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    export = sub.add_parser("export", help="convert JSON to catalog.txt")
    export.add_argument("json_path", nargs="?", type=Path, default=DEFAULT_JSON)
    export.add_argument("txt_path", nargs="?", type=Path, default=DEFAULT_TXT)

    build = sub.add_parser("build", help="convert catalog.txt to normalized JSON")
    build.add_argument("txt_path", nargs="?", type=Path, default=DEFAULT_TXT)
    build.add_argument("json_path", nargs="?", type=Path, default=DEFAULT_JSON)

    check = sub.add_parser("check", help="validate catalog.txt")
    check.add_argument("txt_path", nargs="?", type=Path, default=DEFAULT_TXT)

    apply_cldr = sub.add_parser("apply-cldr", help="apply cached CLDR Chinese TTS names to #@symbols")
    apply_cldr.add_argument("txt_path", nargs="?", type=Path, default=DEFAULT_TXT)

    dedupe = sub.add_parser("dedupe", help="normalize/deduplicate a JSON catalog")
    dedupe.add_argument("input_path", nargs="?", type=Path, default=DEFAULT_JSON)
    dedupe.add_argument("output_path", nargs="?", type=Path, default=DEFAULT_JSON)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "export":
            return command_export(args.json_path, args.txt_path)
        if args.command == "build":
            return command_build(args.txt_path, args.json_path)
        if args.command == "check":
            return command_check(args.txt_path)
        if args.command == "apply-cldr":
            return command_apply_cldr(args.txt_path)
        if args.command == "dedupe":
            return command_dedupe(args.input_path, args.output_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
