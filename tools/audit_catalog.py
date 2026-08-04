#!/usr/bin/env python3
"""Audit generated YeSymbol catalog invariants that should never regress."""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence, Set, Tuple

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "data-source" / "catalog.generated.json"
SERIES_MANIFEST = ROOT / "data-source" / "catalog-series-manifest.json"


def load_series() -> Dict[str, Set[str]]:
    payload = json.loads(SERIES_MANIFEST.read_text(encoding="utf-8"))
    return {
        name: {chr(int(value, 16)) for value in values}
        for name, values in payload.get("series", {}).items()
    }


def category(data: Dict[str, Any], name: str) -> Dict[str, Any]:
    for item in data.get("categories", []):
        if item.get("name") == name:
            return item
    raise AssertionError(f"missing category: {name}")


def category_items(cat: Dict[str, Any]) -> List[str]:
    return [item for group in cat.get("groups", []) if not group.get("spacer") for item in group.get("items", [])]


def block_items(cat: Dict[str, Any], title: str) -> List[str]:
    output: List[str] = []
    active = False
    for group in cat.get("groups", []):
        if group.get("spacer"):
            if active:
                break
            continue
        group_title = str(group.get("title", ""))
        if group_title:
            if active:
                break
            active = group_title == title
        if active:
            output.extend(group.get("items", []))
    if not active and not output:
        raise AssertionError(f"missing group: {cat.get('name')}/{title}")
    return output


def regional_flag(text: str) -> bool:
    return len(text) == 2 and all(0x1F1E6 <= ord(ch) <= 0x1F1FF for ch in text)


def tag_flag(text: str) -> bool:
    return len(text) >= 3 and ord(text[0]) == 0x1F3F4 and ord(text[-1]) == 0xE007F


def require_exact(label: str, actual: Iterable[str], expected: Set[str], errors: List[str]) -> None:
    actual_set = set(actual)
    if actual_set != expected:
        missing = sorted(expected - actual_set, key=lambda text: tuple(map(ord, text)))
        extra = sorted(actual_set - expected, key=lambda text: tuple(map(ord, text)))
        errors.append(f"{label}: expected {len(expected)}, got {len(actual_set)}; missing={missing[:8]!r}, extra={extra[:8]!r}")


def main() -> int:
    data = json.loads(CATALOG.read_text(encoding="utf-8"))
    series = load_series()
    errors: List[str] = []

    names = [item.get("name") for item in data.get("categories", [])]
    for required in ["序号字母", "东亚字符", "大篆", "小篆", "俄文字符", "象形文字", "古埃及文字"]:
        if required not in names:
            errors.append(f"missing category: {required}")

    number_cat = category(data, "序号字母")
    circled_21_50 = set(chr(cp) for cp in range(0x3251, 0x3260)) | set(chr(cp) for cp in range(0x32B1, 0x32C0))
    require_exact("带圈数字 21–50", block_items(number_cat, "带圈数字 21–50"), circled_21_50, errors)

    letter_cat = number_cat
    expected_letters: List[str] = []
    for start, end in (
        (0x24B6, 0x24D0),
        (0x24D0, 0x24EA),
        (0x249C, 0x24B6),
        (0x1F110, 0x1F12A),
        (0x1F130, 0x1F14A),
        (0x1F150, 0x1F16A),
        (0x1F170, 0x1F18A),
    ):
        expected_letters.extend(chr(cp) for cp in range(start, end))
    actual_letters = block_items(letter_cat, "字母序号")
    if actual_letters != expected_letters:
        errors.append(f"序号字母/字母序号 should contain seven complete A-Z series in order; got {len(actual_letters)} items")

    for seal_name in ("大篆", "小篆"):
        seal = category(data, seal_name)
        if category_items(seal):
            errors.append(f"{seal_name} must not fake separate Unicode code points; keep it as a font-dependent reserved category")
        if "Unicode" not in str(seal.get("desc", "")):
            errors.append(f"{seal_name} description must explain the Unicode/font limitation")

    special = category(data, "特殊符号")
    require_exact("麻将牌", block_items(special, "麻将牌（Unicode 顺序）"), series["mahjong"], errors)
    require_exact("多米诺骨牌", block_items(special, "多米诺骨牌（Unicode 顺序）"), series["domino"], errors)
    require_exact("扑克牌", block_items(special, "扑克牌（Unicode 顺序）"), series["playing_cards"], errors)
    require_exact("国际象棋扩展符号", block_items(special, "国际象棋扩展符号（Unicode 顺序）"), series["chess_symbols"], errors)

    pictographic = category(data, "象形文字")
    pictographic_items = set(category_items(pictographic))
    for label, key in (
        ("斐斯托斯圆盘文字", "phaistos"),
        ("楔形文字", "cuneiform"),
        ("楔形数字与标点", "cuneiform_numbers"),
        ("安纳托利亚象形文字", "anatolian_hieroglyphs"),
    ):
        expected = series[key]
        if not expected.issubset(pictographic_items):
            errors.append(f"象形文字 missing items from {label}: {len(expected - pictographic_items)}")

    egyptian = category(data, "古埃及文字")
    egyptian_items = set(category_items(egyptian))
    require_exact("古埃及文字", egyptian_items, series["egyptian_hieroglyphs_and_controls"], errors)

    flags = category(data, "Emoji·符号与旗帜")
    region_items = block_items(flags, "国家和地区旗帜（Unicode 17.0）")
    tag_items = block_items(flags, "地区旗帜（英格兰、苏格兰、威尔士）")
    if len(region_items) != 259 or len(set(region_items)) != 259 or not all(regional_flag(item) for item in region_items):
        errors.append(f"regional flags should contain 259 unique RGI sequences, got {len(region_items)}/{len(set(region_items))}")
    territories = json.loads((ROOT / "data-source" / "territory_names_zh.json").read_text(encoding="utf-8"))
    pseudo_regions = {"XA", "XB", "QO", "EZ", "ZZ"}
    expected_codes = {code for code in territories if len(code) == 2 and code.isalpha() and code.isupper() and code not in pseudo_regions}
    actual_codes = {"".join(chr(ord("A") + ord(ch) - 0x1F1E6) for ch in flag) for flag in region_items}
    if actual_codes != expected_codes:
        errors.append(f"regional flag codes differ from territory table: missing={sorted(expected_codes-actual_codes)[:8]!r}, extra={sorted(actual_codes-expected_codes)[:8]!r}")
    if len(tag_items) != 3 or len(set(tag_items)) != 3 or not all(tag_flag(item) for item in tag_items):
        errors.append(f"subdivision tag flags should contain 3 unique sequences, got {len(tag_items)}/{len(set(tag_items))}")
    sark = chr(0x1F1E8) + chr(0x1F1F6)
    if sark not in region_items:
        errors.append("missing Sark flag (CQ)")
    records_by_text = {record.get("text"): record for record in data.get("symbols", [])}
    bad_flag_names = [
        flag for flag in region_items + tag_items
        if "旗" not in str(records_by_text.get(flag, {}).get("name_zh", ""))
        or not records_by_text.get(flag, {}).get("emoji")
    ]
    if bad_flag_names:
        errors.append(f"flag records need Chinese flag names and Emoji flags: {bad_flag_names[:8]!r}")

    supplement = category(data, "补充符号")
    bad_prefixes = ("标点与排版 ·", "括号与引号 ·", "货币与单位 ·", "箭头与方向 ·", "图形与制表 ·", "数学符号 ·", "数字·分数·编号 ·", "拉丁扩展与音标 ·")
    residual_titles = [str(group.get("title", "")) for group in supplement.get("groups", [])]
    leaked = [title for title in residual_titles if title.startswith(bad_prefixes)]
    if leaked:
        errors.append(f"supplement still contains groups that should be classified above: {leaked[:8]!r}")
    # Build-generated orphan placement is allowed and required: every ordinary
    # searchable symbol needs a real source row for tooltip metadata and
    # “跳到所在位置”.  Only Emoji presentation/skin variants may remain hidden.
    all_visible = {
        item
        for cat in data.get("categories", [])[1:]
        for group in cat.get("groups", [])
        for item in group.get("items", [])
    }
    def variant_key(text: str) -> str:
        return "".join(
            char for char in str(text)
            if ord(char) != 0xFE0F and not (0x1F3FB <= ord(char) <= 0x1F3FF)
        )
    visible_keys = {variant_key(item) for item in all_visible}
    unlocatable = [
        record.get("text") for record in data.get("symbols", [])
        if record.get("text") not in all_visible
        and variant_key(record.get("text", "")) not in visible_keys
    ]
    if unlocatable:
        errors.append(f"searchable symbols without a source location: {unlocatable[:8]!r} (total {len(unlocatable)})")
    misplaced = {"萨顿手语书写", "带圈表意文字补充", "杂项符号和象形文字", "装饰性印刷符号", "交通和地图符号", "补充符号和象形文字", "传统计算机符号"}
    leaked_exact = sorted(misplaced.intersection(residual_titles))
    if leaked_exact:
        errors.append(f"supplement still has movable groups: {leaked_exact!r}")

    symbol_records = data.get("symbols", [])
    bad_names = [record.get("text") for record in symbol_records if not record.get("name_zh") or record.get("name_zh") == "未标注" or not record.get("name_en")]
    if bad_names:
        errors.append(f"missing bilingual names: {bad_names[:12]!r} (total {len(bad_names)})")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(
        "catalog audit passed: "
        f"{len(symbol_records)} symbol records, 259 regional flags + 3 subdivision flags, "
        "complete 21–50 circled numbers, complete card/tile series, "
        f"{len(egyptian_items)} Egyptian characters, all searchable records locatable"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
