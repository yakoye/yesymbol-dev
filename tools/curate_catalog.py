#!/usr/bin/env python3
"""One-time/maintainer curation for the YeSymbol human catalog.

This script reorganizes the large legacy supplement section, canonicalizes
well-defined Unicode series, and adds ancient writing-system categories.  It is
not run automatically during normal builds; catalog.txt remains the hand-edited
source after this migration.
"""
from __future__ import annotations

import copy
import importlib.util
import re
import unicodedata
from collections import OrderedDict
from pathlib import Path
from typing import Any, Dict, Iterable, List, MutableMapping, Sequence, Tuple

ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "data-source" / "catalog.txt"

spec = importlib.util.spec_from_file_location("catalog_text", ROOT / "tools" / "catalog_text.py")
ct = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(ct)


def assigned(start: int, end: int) -> List[str]:
    result: List[str] = []
    for cp in range(start, end + 1):
        ch = chr(cp)
        try:
            unicodedata.name(ch)
        except ValueError:
            continue
        result.append(ch)
    return result


def codepoint_key(text: str) -> Tuple[int, ...]:
    return tuple(ord(ch) for ch in text)


def blockify(groups: Sequence[MutableMapping[str, Any]]) -> List[List[Dict[str, Any]]]:
    blocks: List[List[Dict[str, Any]]] = []
    current: List[Dict[str, Any]] = []
    for raw in groups:
        group = copy.deepcopy(dict(raw))
        if group.get("spacer"):
            if current:
                blocks.append(current)
                current = []
            blocks.append([group])
            continue
        if str(group.get("title", "")).strip():
            if current:
                blocks.append(current)
            current = [group]
        else:
            if not current:
                current = [group]
            else:
                current.append(group)
    if current:
        blocks.append(current)
    return blocks


def clean_block_title(block: List[Dict[str, Any]], title: str) -> List[Dict[str, Any]]:
    block = copy.deepcopy(block)
    if block and not block[0].get("spacer"):
        block[0]["title"] = title
    return block


def category_map(data: MutableMapping[str, Any]) -> Dict[str, Dict[str, Any]]:
    return {str(category.get("name", "")): category for category in data.get("categories", [])}


def remove_items(categories: Sequence[MutableMapping[str, Any]], forbidden: set[str]) -> None:
    for category in categories:
        new_groups: List[Dict[str, Any]] = []
        for raw in category.get("groups", []):
            group = copy.deepcopy(dict(raw))
            if group.get("spacer"):
                new_groups.append(group)
                continue
            group["items"] = [item for item in group.get("items", []) if item not in forbidden]
            if group["items"]:
                new_groups.append(group)
        category["groups"] = new_groups


def append_block(category: MutableMapping[str, Any], title: str, items: Sequence[str], add_spacer: bool = False) -> None:
    items = list(items)
    if not items:
        return
    if add_spacer and category.get("groups"):
        category["groups"].append({"title": "", "cols": 0, "row": 0, "spacer": True, "items": []})
    for index, row in enumerate(ct.chunks(items, ct.MAX_COLUMNS)):
        category["groups"].append(
            {
                "title": title if index == 0 else "",
                "cols": len(row),
                "row": 0,
                "items": list(row),
            }
        )


def upsert_record(records: "OrderedDict[str, Dict[str, Any]]", text: str, name_zh: str, name_en: str, emoji: bool = False) -> None:
    if text in records:
        record = records[text]
        record["name_zh"] = name_zh or record.get("name_zh", "")
        record["name_en"] = name_en or record.get("name_en", record.get("name", ""))
        record["name"] = record["name_en"]
        record["emoji"] = bool(emoji or record.get("emoji", False))
    else:
        records[text] = {
            "text": text,
            "name": name_en,
            "emoji": bool(emoji),
            "name_en": name_en,
            "name_zh": name_zh,
        }


CN_DIGITS = "零一二三四五六七八九"


def cn_number(value: int) -> str:
    if value < 10:
        return CN_DIGITS[value]
    if value == 10:
        return "十"
    if value < 20:
        return "十" + CN_DIGITS[value - 10]
    tens, ones = divmod(value, 10)
    return CN_DIGITS[tens] + "十" + (CN_DIGITS[ones] if ones else "")


PHAISTOS_ZH = {
    "PEDESTRIAN": "行人",
    "PLUMED HEAD": "羽饰头像",
    "TATTOOED HEAD": "刺青头像",
    "CAPTIVE": "俘虏",
    "CHILD": "儿童",
    "WOMAN": "女性",
    "HELMET": "头盔",
    "GAUNTLET": "护手",
    "TIARA": "冠饰",
    "ARROW": "箭",
    "BOW": "弓",
    "SHIELD": "盾牌",
    "CLUB": "棍棒",
    "MANACLES": "镣铐",
    "MATTOCK": "鹤嘴锄",
    "SAW": "锯",
    "LID": "盖子",
    "BOOMERANG": "回旋镖",
    "CARPENTRY PLANE": "木工刨",
    "DOLIUM": "大陶罐",
    "COMB": "梳子",
    "SLING": "投石索",
    "COLUMN": "柱子",
    "BEEHIVE": "蜂巢",
    "SHIP": "船",
    "HORN": "角",
    "HIDE": "兽皮",
    "BULLS LEG": "公牛腿",
    "CAT": "猫",
    "RAM": "公羊",
    "EAGLE": "鹰",
    "DOVE": "鸽子",
    "TUNNY": "金枪鱼",
    "BEE": "蜜蜂",
    "PLANE TREE": "悬铃木",
    "VINE": "葡萄藤",
    "PAPYRUS": "纸莎草",
    "ROSETTE": "蔷薇花饰",
    "LILY": "百合",
    "OX BACK": "牛背",
    "FLUTE": "长笛",
    "GRATER": "擦菜板",
    "STRAINER": "滤器",
    "SMALL AXE": "小斧",
    "WAVY BAND": "波浪带",
    "COMBINING OBLIQUE STROKE": "组合斜划",
}

EGYPTIAN_GROUP_ZH = {
    "A": "A类·人与职业",
    "B": "B类·女性与活动",
    "C": "C类·人形神祇",
    "D": "D类·人体部位",
    "E": "E类·哺乳动物",
    "F": "F类·哺乳动物部位",
    "G": "G类·鸟类",
    "H": "H类·鸟类部位",
    "I": "I类·两栖与爬行动物",
    "K": "K类·鱼类及部位",
    "L": "L类·无脊椎与小动物",
    "M": "M类·树木与植物",
    "N": "N类·天空、土地与水",
    "NL": "NL类·下埃及地区标志",
    "NU": "NU类·上埃及地区标志",
    "O": "O类·建筑及部件",
    "P": "P类·船舶及部件",
    "Q": "Q类·家具与葬具",
    "R": "R类·神庙器物与神圣标志",
    "S": "S类·皇冠、服饰与权杖",
    "T": "T类·战争、狩猎与屠宰",
    "U": "U类·农业、工艺与职业",
    "V": "V类·绳索、纤维、篮筐与袋",
    "W": "W类·石器与陶器",
    "X": "X类·面包与糕点",
    "Y": "Y类·书写、游戏与音乐",
    "Z": "Z类·笔画与几何图形",
    "AA": "AA类·未分类符号",
}

EGYPTIAN_CONTROL_ZH = {
    "VERTICAL JOINER": "纵向连接符",
    "HORIZONTAL JOINER": "横向连接符",
    "INSERT AT TOP START": "在起始顶部插入",
    "INSERT AT BOTTOM START": "在起始底部插入",
    "INSERT AT TOP END": "在结束顶部插入",
    "INSERT AT BOTTOM END": "在结束底部插入",
    "OVERLAY MIDDLE": "中部叠加",
    "BEGIN SEGMENT": "开始片段",
    "END SEGMENT": "结束片段",
    "INSERT AT MIDDLE": "在中部插入",
    "INSERT AT TOP": "在顶部插入",
    "INSERT AT BOTTOM": "在底部插入",
    "BEGIN ENCLOSURE": "开始围框",
    "END ENCLOSURE": "结束围框",
    "BEGIN WALLED ENCLOSURE": "开始有墙围框",
    "END WALLED ENCLOSURE": "结束有墙围框",
    "MIRROR HORIZONTALLY": "水平镜像",
    "FULL BLANK": "全空白",
    "HALF BLANK": "半空白",
    "LOST SIGN": "缺失符号",
    "HALF LOST SIGN": "半缺失符号",
    "TALL LOST SIGN": "高形缺失符号",
    "WIDE LOST SIGN": "宽形缺失符号",
    "MODIFIER DAMAGED AT TOP START": "损坏修饰·起始顶部",
    "MODIFIER DAMAGED AT BOTTOM START": "损坏修饰·起始底部",
    "MODIFIER DAMAGED AT START": "损坏修饰·起始",
    "MODIFIER DAMAGED AT TOP END": "损坏修饰·结束顶部",
    "MODIFIER DAMAGED AT TOP": "损坏修饰·顶部",
    "MODIFIER DAMAGED AT BOTTOM START AND TOP END": "损坏修饰·起始底部与结束顶部",
    "MODIFIER DAMAGED AT START AND TOP": "损坏修饰·起始与顶部",
    "MODIFIER DAMAGED AT BOTTOM END": "损坏修饰·结束底部",
    "MODIFIER DAMAGED AT TOP START AND BOTTOM END": "损坏修饰·起始顶部与结束底部",
    "MODIFIER DAMAGED AT BOTTOM": "损坏修饰·底部",
    "MODIFIER DAMAGED AT START AND BOTTOM": "损坏修饰·起始与底部",
    "MODIFIER DAMAGED AT END": "损坏修饰·结束",
    "MODIFIER DAMAGED AT TOP AND END": "损坏修饰·顶部与结束",
    "MODIFIER DAMAGED AT BOTTOM AND END": "损坏修饰·底部与结束",
    "MODIFIER DAMAGED": "损坏修饰",
}


def create_ancient_categories(records: "OrderedDict[str, Dict[str, Any]]") -> Tuple[Dict[str, Any], Dict[str, Any], set[str]]:
    pictographic = {"name": "象形文字", "desc": "古代象形及早期书写系统，按 Unicode 码位顺序排列。", "groups": []}
    egyptian = {"name": "古埃及文字", "desc": "古埃及圣书体及其排版控制字符，按 Gardiner 类别和 Unicode 顺序排列。", "groups": []}
    all_new: set[str] = set()

    phaistos = assigned(0x101D0, 0x101FD)
    append_block(pictographic, "斐斯托斯圆盘文字", phaistos)
    for ch in phaistos:
        name_en = unicodedata.name(ch)
        suffix = name_en.replace("PHAISTOS DISC SIGN ", "")
        name_zh = "斐斯托斯圆盘符号·" + PHAISTOS_ZH.get(suffix, suffix)
        upsert_record(records, ch, name_zh, name_en, False)
    all_new.update(phaistos)

    anatolian = assigned(0x14400, 0x1467F)
    append_block(pictographic, "安纳托利亚象形文字", anatolian, add_spacer=True)
    for ch in anatolian:
        name_en = unicodedata.name(ch)
        code = name_en.replace("ANATOLIAN HIEROGLYPH ", "")
        upsert_record(records, ch, f"安纳托利亚象形文字 {code}", name_en, False)
    all_new.update(anatolian)

    cuneiform = assigned(0x12000, 0x123FF)
    append_block(pictographic, "楔形文字", cuneiform, add_spacer=True)
    for ch in cuneiform:
        name_en = unicodedata.name(ch)
        suffix = name_en.replace("CUNEIFORM SIGN ", "")
        upsert_record(records, ch, f"楔形文字符号 {suffix}", name_en, False)
    all_new.update(cuneiform)

    cuneiform_numbers = assigned(0x12400, 0x1247F)
    append_block(pictographic, "楔形数字与标点", cuneiform_numbers, add_spacer=True)
    for ch in cuneiform_numbers:
        name_en = unicodedata.name(ch)
        if "NUMERIC SIGN" in name_en:
            suffix = name_en.split("NUMERIC SIGN ", 1)[1]
            name_zh = f"楔形数字 {suffix}"
        elif "PUNCTUATION SIGN" in name_en:
            suffix = name_en.split("PUNCTUATION SIGN ", 1)[1]
            name_zh = f"楔形文字标点 {suffix}"
        else:
            name_zh = "楔形文字 " + name_en
        upsert_record(records, ch, name_zh, name_en, False)
    all_new.update(cuneiform_numbers)

    egypt_items = assigned(0x13000, 0x1342F)
    by_prefix: "OrderedDict[str, List[str]]" = OrderedDict()
    for ch in egypt_items:
        name_en = unicodedata.name(ch)
        code = name_en.replace("EGYPTIAN HIEROGLYPH ", "")
        match = re.match(r"([A-Z]+)", code)
        prefix = match.group(1) if match else "OTHER"
        by_prefix.setdefault(prefix, []).append(ch)
        upsert_record(records, ch, f"古埃及象形文字 {code}", name_en, False)
    for prefix, items in by_prefix.items():
        append_block(egyptian, EGYPTIAN_GROUP_ZH.get(prefix, f"{prefix}类"), items, add_spacer=bool(egyptian["groups"]))
    all_new.update(egypt_items)

    controls = assigned(0x13430, 0x1345F)
    append_block(egyptian, "排版与组合控制", controls, add_spacer=True)
    for ch in controls:
        name_en = unicodedata.name(ch)
        suffix = name_en.replace("EGYPTIAN HIEROGLYPH ", "")
        upsert_record(records, ch, "古埃及文字格式控制·" + EGYPTIAN_CONTROL_ZH.get(suffix, suffix), name_en, False)
    all_new.update(controls)

    return pictographic, egyptian, all_new


def regional_flag(code: str) -> str:
    return "".join(chr(0x1F1E6 + ord(letter) - ord("A")) for letter in code)


def is_regional_flag(text: str) -> bool:
    return len(text) == 2 and all(0x1F1E6 <= ord(ch) <= 0x1F1FF for ch in text)


def is_tag_flag(text: str) -> bool:
    return len(text) >= 3 and ord(text[0]) == 0x1F3F4 and ord(text[-1]) == 0xE007F


def main() -> int:
    data = ct.parse_text(CATALOG_PATH)
    cats = category_map(data)
    required = ["特殊符号", "标点符号", "序号字母", "数学/单位", "希腊/拉丁", "制表符", "补充符号", "Emoji·符号与旗帜"]
    missing = [name for name in required if name not in cats]
    if missing:
        raise SystemExit("missing required categories: " + ", ".join(missing))

    records: "OrderedDict[str, Dict[str, Any]]" = OrderedDict((str(r["text"]), copy.deepcopy(r)) for r in data["symbols"])
    pictographic, egyptian, ancient_items = create_ancient_categories(records)

    # Reorganize titled legacy blocks out of 补充符号.
    supplement = cats["补充符号"]
    destination_started: set[str] = set()
    kept_blocks: List[List[Dict[str, Any]]] = []

    def move_block(target_name: str, title: str, block: List[Dict[str, Any]]) -> None:
        target = cats.get(target_name)
        if target is None:
            raise RuntimeError(f"unknown target category {target_name}")
        if target_name not in destination_started and target.get("groups"):
            target["groups"].append({"title": "", "cols": 0, "row": 0, "spacer": True, "items": []})
            destination_started.add(target_name)
        target["groups"].extend(clean_block_title(block, title))

    for block in blockify(supplement.get("groups", [])):
        first = block[0]
        if first.get("spacer"):
            # Keep at most one intentional separator in the residual supplement.
            if kept_blocks and not kept_blocks[-1][0].get("spacer"):
                kept_blocks.append(block)
            continue
        title = str(first.get("title", "")).strip()
        if title == "未分类补充":
            # Records remain in #@symbols and are compiled/searchable, but this
            # giant Emoji-variant dump is deliberately hidden from the UI.
            continue

        target_name = ""
        new_title = title
        prefix = title.split(" · ", 1)[0] if " · " in title else title
        suffix = title.split(" · ", 1)[1] if " · " in title else title

        direct_moves = {
            "萨顿手语书写": ("象形文字", "萨顿手语书写"),
            "Ottoman Siyaq Numbers": ("序号字母", "奥斯曼西亚克数字"),
            "带圈表意文字补充": ("东亚字符", "带圈表意文字补充"),
            "杂项符号和象形文字": ("特殊符号", "杂项符号和象形文字"),
            "装饰性印刷符号": ("特殊符号", "装饰性印刷符号"),
            "交通和地图符号": ("特殊符号", "交通和地图符号"),
            "补充符号和象形文字": ("特殊符号", "补充符号和象形文字"),
            "传统计算机符号": ("制表符", "传统计算机符号"),
        }
        if title in direct_moves:
            target_name, new_title = direct_moves[title]
        elif prefix in ("标点与排版", "括号与引号"):
            target_name, new_title = "标点符号", suffix
        elif prefix in ("货币与单位", "数学符号"):
            target_name, new_title = "数学/单位", suffix
        elif prefix == "箭头与方向":
            target_name, new_title = "特殊符号", suffix
        elif prefix == "数字·分数·编号":
            target_name, new_title = "序号字母", suffix
        elif prefix == "拉丁扩展与音标":
            target_name, new_title = "希腊/拉丁", suffix
        elif prefix == "图形与制表":
            if any(word in suffix for word in ("制表符", "块元素", "盲文", "控制字符", "传统计算机")):
                target_name = "制表符"
            else:
                target_name = "特殊符号"
            new_title = suffix
        elif title in ("宗教、文化与公共标识", "办公与通信"):
            target_name = "特殊符号"
        elif prefix == "技术与补充符号":
            if suffix == "原有常用集合":
                target_name, new_title = "特殊符号", "技术括角与定位符号"
            elif any(word in suffix for word in ("杂项技术符号", "控制字符图示", "光学字符识别")):
                target_name, new_title = "制表符", suffix
            elif suffix in ("Miscellaneous Symbols", "印刷符号", "特殊字符"):
                target_name, new_title = "特殊符号", {
                    "Miscellaneous Symbols": "杂项符号",
                    "印刷符号": "印刷与装饰符号",
                    "特殊字符": "特殊占位字符",
                }[suffix]
            elif suffix == "补充标点":
                target_name, new_title = "标点符号", suffix
            elif suffix == "科普特文":
                target_name, new_title = "希腊/拉丁", suffix
            elif suffix == "彝文部首":
                target_name, new_title = "东亚字符", suffix
            elif any(word in suffix for word in ("易经", "太玄经", "斐斯托斯圆盘", "古代符号")):
                # Re-added below to the dedicated pictographic category when
                # applicable; keep only unique non-canonical items.
                target_name, new_title = "象形文字", suffix
            elif any(word in suffix for word in ("麻将牌", "多米诺骨牌", "扑克牌", "国际象棋", "音乐符号", "音乐记谱", "炼金术")):
                target_name, new_title = "特殊符号", suffix
            elif any(word in suffix for word in ("数字", "西亚克", "上标和下标")):
                target_name, new_title = "序号字母", suffix
            elif any(word in suffix for word in ("拉丁文", "希腊文", "西里尔文", "间距修饰字母", "声调修饰字母", "字母式符号")):
                target_name, new_title = "希腊/拉丁", suffix
            elif any(word in suffix for word in ("半角及全角形式", "小型变体")):
                target_name, new_title = "标点符号", suffix

        residual_title_map = {
            "Oriya": "奥里亚文",
            "Tamil": "泰米尔文",
            "Malayalam": "马拉雅拉姆文",
            "New Tai Lue": "新傣仂文",
            "Syloti Nagri": "锡尔赫特文",
            "Myanmar Extended-A": "缅甸文扩展-A",
            "Palmyrene": "帕尔米拉文",
            "Other": "其他历史符号",
        }
        if not target_name and prefix == "技术与补充符号":
            block = clean_block_title(block, residual_title_map.get(suffix, suffix))

        if target_name == "象形文字":
            # The dedicated category is not in cats yet.  Preserve only material
            # outside the canonical ranges; canonical sets are added below.
            block_items = [item for group in block for item in group.get("items", []) if item not in ancient_items]
            if block_items:
                append_block(pictographic, new_title, block_items, add_spacer=True)
            continue
        if target_name:
            move_block(target_name, new_title, block)
        else:
            kept_blocks.append(block)

    supplement["groups"] = [group for block in kept_blocks for group in block]
    supplement["desc"] = "尚未归入常用分类的技术符号、历史文字与其他补充内容。"

    # Canonical series: remove every old partial/split occurrence before adding
    # one complete Unicode-ordered group.
    circled_21_50 = [chr(cp) for cp in range(0x3251, 0x3260)] + [chr(cp) for cp in range(0x32B1, 0x32C0)]
    mahjong = assigned(0x1F000, 0x1F02F)
    domino = assigned(0x1F030, 0x1F09F)
    playing_cards = assigned(0x1F0A0, 0x1F0FF)
    chess = assigned(0x1FA00, 0x1FA6F)
    canonical = set(circled_21_50 + mahjong + domino + playing_cards + chess) | ancient_items
    remove_items(data["categories"], canonical)

    append_block(cats["序号字母"], "带圈数字 21–50", circled_21_50, add_spacer=True)
    for index, ch in enumerate(circled_21_50, 21):
        name_en = unicodedata.name(ch)
        upsert_record(records, ch, f"带圈数字{cn_number(index)}", name_en, False)

    append_block(cats["特殊符号"], "麻将牌（Unicode 顺序）", mahjong, add_spacer=True)
    append_block(cats["特殊符号"], "多米诺骨牌（Unicode 顺序）", domino, add_spacer=True)
    append_block(cats["特殊符号"], "扑克牌（Unicode 顺序）", playing_cards, add_spacer=True)
    append_block(cats["特殊符号"], "国际象棋扩展符号（Unicode 顺序）", chess, add_spacer=True)
    for ch in mahjong:
        name_en = unicodedata.name(ch)
        upsert_record(records, ch, "麻将牌·" + name_en.replace("MAHJONG TILE ", ""), name_en, False)
    for ch in domino:
        name_en = unicodedata.name(ch)
        upsert_record(records, ch, "多米诺骨牌·" + name_en.replace("DOMINO TILE ", ""), name_en, False)
    for ch in playing_cards:
        name_en = unicodedata.name(ch)
        upsert_record(records, ch, "扑克牌·" + name_en.replace("PLAYING CARD ", ""), name_en, True if ch == "🃏" else False)
    for ch in chess:
        name_en = unicodedata.name(ch)
        upsert_record(records, ch, "国际象棋符号·" + name_en.replace("NEUTRAL CHESS ", "").replace("WHITE CHESS ", "白方").replace("BLACK CHESS ", "黑方"), name_en, False)

    # Flags: Unicode Emoji 17.0 defines 259 RGI regional flags.  The user's
    # previous list had 258 and missed CQ (Sark), introduced in Emoji 16.0.
    flag_category = cats["Emoji·符号与旗帜"]
    existing_tag = set()
    for group in flag_category.get("groups", []):
        for item in group.get("items", []):
            if is_tag_flag(item):
                existing_tag.add(item)

    # Generate the complete set from the pinned Chinese territory-name table,
    # rather than trusting a previous partial visual list.  CLDR pseudo regions
    # XA/XB/QO/EZ/ZZ do not have RGI flag sequences.
    import json
    territory_path = ROOT / "data-source" / "territory_names_zh.json"
    territories = json.loads(territory_path.read_text(encoding="utf-8")) if territory_path.exists() else {}
    territories["CQ"] = "萨克岛"
    pseudo_regions = {"XA", "XB", "QO", "EZ", "ZZ"}
    region_codes = sorted(code for code in territories if len(code) == 2 and code.isalpha() and code.isupper() and code not in pseudo_regions)
    region_flags = [regional_flag(code) for code in region_codes]
    tag_flags = sorted(existing_tag, key=codepoint_key)
    if len(region_flags) != 259:
        raise RuntimeError(f"expected 259 regional flags from territory names, got {len(region_flags)}")
    if len(tag_flags) != 3:
        raise RuntimeError(f"expected 3 subdivision tag flags, got {len(tag_flags)}")

    remove_items([flag_category], set(region_flags + tag_flags))
    new_flag_groups: List[Dict[str, Any]] = []
    temp_flag_category = {"groups": new_flag_groups}
    append_block(temp_flag_category, "国家和地区旗帜（Unicode 17.0）", region_flags)
    append_block(temp_flag_category, "地区旗帜（英格兰、苏格兰、威尔士）", tag_flags, add_spacer=True)
    flag_category["groups"] = new_flag_groups + flag_category.get("groups", [])

    territory_path.write_text(json.dumps(territories, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for flag in region_flags:
        code = "".join(chr(ord("A") + ord(ch) - 0x1F1E6) for ch in flag)
        place = territories.get(code, code)
        upsert_record(records, flag, f"{place}国旗", f"FLAG: {place.upper() if place.isascii() else code}", True)

    tag_names = {
        "gbeng": ("英格兰旗帜", "FLAG: ENGLAND"),
        "gbsct": ("苏格兰旗帜", "FLAG: SCOTLAND"),
        "gbwls": ("威尔士旗帜", "FLAG: WALES"),
    }
    for flag in tag_flags:
        tag_code = "".join(chr(ord(ch) - 0xE0000) for ch in flag[1:-1])
        name_zh, name_en = tag_names.get(tag_code, (f"地区旗帜 {tag_code}", f"SUBDIVISION FLAG: {tag_code.upper()}"))
        upsert_record(records, flag, name_zh, name_en, True)

    # Insert the two new first-level categories immediately before 补充符号.
    categories = [category for category in data["categories"] if category.get("name") not in ("象形文字", "古埃及文字")]
    insert_at = next(index for index, category in enumerate(categories) if category.get("name") == "补充符号")
    categories[insert_at:insert_at] = [pictographic, egyptian]
    data["categories"] = categories
    data["symbols"] = list(records.values())

    ct.export_text(data, CATALOG_PATH)
    print(f"curated {CATALOG_PATH}")
    print(f"symbols in annotation table: {len(records)}")
    print(f"regional flags: {len(region_flags)}; tag flags: {len(tag_flags)}")
    print(f"Egyptian hieroglyphs: {len(assigned(0x13000, 0x1342F))}; controls: {len(assigned(0x13430, 0x1345F))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
