from __future__ import annotations

from pathlib import Path
import importlib.util
import json
import re
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[1]
D = json.loads((R / "data-source/catalog.generated.json").read_text(encoding="utf-8"))

# Catalog basics.
assert len(D["symbols"]) == 16679
texts = [item["text"] for item in D["symbols"]]
assert len(texts) == len(set(texts))
assert all(item.get("name_zh") and item.get("name_en") for item in D["symbols"])
assert D["categories"][0].get("role") == "all"
assert D["categories"][0]["name"] == "全部符号"

for category in D["categories"][1:]:
    seen = set()
    row = 0
    for group in category.get("groups", []):
        if group.get("spacer"):
            continue
        row += 1
        items = list(group.get("items", []))
        assert 1 <= int(group.get("cols", 0)) <= 12
        assert len(items) <= 12
        assert int(group.get("row", 0)) == row
        assert len(items) == len(set(items))
        assert not seen.intersection(items)
        seen.update(items)

main_indices = D.get("ui_main_category_indices", [])
emoji_indices = D.get("ui_emoji_category_indices", [])
other_indices = D.get("ui_other_category_indices", [])
assert len(main_indices) == len(set(main_indices))
assert len(emoji_indices) == len(set(emoji_indices))
assert len(other_indices) == len(set(other_indices))
assert not set(main_indices).intersection(emoji_indices)
assert not set(main_indices).intersection(other_indices)
assert not set(emoji_indices).intersection(other_indices)
assert all(D["categories"][i].get("ui_section") == "main" for i in main_indices)
assert all(D["categories"][i].get("ui_section") == "emoji" for i in emoji_indices)
assert all(D["categories"][i].get("ui_section") == "other" for i in other_indices)
assert all(D["categories"][i].get("ui_section") == "hidden"
           for i in range(1, len(D["categories"]))
           if i not in set(main_indices) | set(emoji_indices) | set(other_indices)
           and D["categories"][i].get("role") != "common")
assert [D["categories"][i]["name"] for i in emoji_indices] == [
    "笑脸", "手势", "人物", "人物活动", "家庭", "情绪", "植物",
    "动物", "食物", "活动", "旅行", "物体", "符号", "旗帜",
]
assert D["categories"][other_indices[0]]["name"] == "日文字符"

# Normal project regeneration checks.
for command in (
    [sys.executable, str(R / "tools/catalog_text.py"), "check"],
    [sys.executable, str(R / "tools/audit_catalog.py")],
):
    result = subprocess.run(
        command,
        cwd=R,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
    )
    assert result.returncode == 0, result.stdout

# Display headings are data, not schema.  Renaming #/##, deleting the empty
# seal-script categories and changing @desc must not break generation/audit.
spec = importlib.util.spec_from_file_location("catalog_text_test", R / "tools/catalog_text.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
audit_spec = importlib.util.spec_from_file_location("audit_test", R / "tools/audit_catalog.py")
audit_module = importlib.util.module_from_spec(audit_spec)
audit_spec.loader.exec_module(audit_module)

text_catalog = (R / "data-source/catalog.txt").read_text(encoding="utf-8")
assert "@ui-section other" in text_catalog
assert "@ui-section emoji" in text_catalog
assert "@ui-section hidden" in text_catalog
assert "@default-common" not in text_catalog
assert "@cols " not in text_catalog and "@row " not in text_catalog
assert "UI_MAIN_CATEGORIES" not in (R / "tools/catalog_text.py").read_text(encoding="utf-8")
assert "UI_OTHER_CATEGORIES" not in (R / "tools/catalog_text.py").read_text(encoding="utf-8")

common_categories = [category for category in D["categories"] if category.get("role") == "common"]
assert len(common_categories) == 1
common_items = [
    item
    for group in common_categories[0].get("groups", [])
    if not group.get("spacer")
    for item in group.get("items", [])
]
assert D["default_common"] == common_items
assert len(common_items) <= 256
assert all(D["categories"][index].get("role") != "common"
           for index in main_indices + emoji_indices + other_indices)

variant = text_catalog.replace("## 扑克牌（Unicode 顺序）", "## 扑克牌")
variant = variant.replace("# 特殊符号\n", "# 任意修改后的一级标题\n", 1)
variant = re.sub(r"\n# 大篆\n.*?(?=\n# 小篆\n)", "\n", variant, flags=re.S)
variant = re.sub(r"\n# 小篆\n.*?(?=\n# 俄文字符\n)", "\n", variant, flags=re.S)
variant = variant.replace(
    "@desc 运算符、关系符号、积分、上下标、分数、单位和货币。",
    "@desc 说明文字可自由修改。",
)
with tempfile.TemporaryDirectory() as temp_dir:
    temp = Path(temp_dir)
    source = temp / "catalog.txt"
    generated = temp / "catalog.generated.json"
    source.write_text(variant, encoding="utf-8")
    parsed = module.parse_text(source)
    normalized, _ = module.normalize_catalog(parsed)
    assert not module.validate_catalog(normalized)
    generated.write_text(json.dumps(normalized, ensure_ascii=False, indent=2), encoding="utf-8")
    audit_module.CATALOG = generated
    assert audit_module.main() == 0
    names = [category["name"] for category in normalized["categories"]]
    assert "大篆" not in names and "小篆" not in names
    assert "任意修改后的一级标题" in names
    first_main = normalized["ui_main_category_indices"][0]
    assert normalized["categories"][first_main]["name"] == "任意修改后的一级标题"

# Desktop C data/UI consume generated category indices, not hard-coded titles.
generator = (R / "tools/generate_bilingual_data.py").read_text(encoding="utf-8")
symbol_h = (R / "include/symbol_data.h").read_text(encoding="utf-8")
generated_c = (R / "src/symbol_data.c").read_text(encoding="ascii")
ui = (R / "src/ui.c").read_text(encoding="utf-8")
clipboard_h = (R / "include/clipboard.h").read_text(encoding="utf-8")
clipboard_c = (R / "src/clipboard.c").read_text(encoding="utf-8")
emoji_renderer_h = (R / "include/emoji_renderer.h").read_text(encoding="utf-8")
emoji_renderer_c = (R / "src/emoji_renderer.c").read_text(encoding="utf-8")
for token in (
    "ui_main_category_indices",
    "ui_emoji_category_indices",
    "ui_other_category_indices",
    "generated-supplement",
    "category.get('role')",
):
    assert token in generator
for token in (
    "g_ys_ui_main_categories",
    "g_ys_ui_emoji_categories",
    "g_ys_ui_other_categories",
    "g_ys_ui_main_category_count",
    "g_ys_ui_emoji_category_count",
    "g_ys_ui_other_category_count",
):
    assert token in symbol_h and token in generated_c and token in ui
assert "g_ys_main_category_names" not in ui
assert "g_ys_other_category_names" not in ui
assert 'wcscmp(ys_pool_string(g_ys_categories[target_category].name_offset), L"补充符号")' not in ui
assert "ys_layout_symbol_range(state, first, state->view_count - first, 12" in ui
assert "ys_layout_symbol_range_fit_columns(state, first" not in ui
assert 'L"其他符号 >"' in ui
assert 'L"其他符号 v"' in ui
assert 'L"Emoji >"' in ui
assert 'L"Emoji v"' in ui
assert "ys_switch_to_adjacent_category" in ui
assert "WM_DPICHANGED" in ui
assert "AdjustWindowRectExForDpi" in ui
assert "GetSystemMetricsForDpi" in ui
assert "ys_clipboard_try_set" in clipboard_h and "ys_clipboard_try_set" in clipboard_c
assert "Sleep(" not in clipboard_c and "SwitchToThread(" not in clipboard_c
assert "YS_TIMER_CLIPBOARD" in ui
assert ui.count("case WM_MOUSEACTIVATE:") >= 2
assert ui.count("return MA_NOACTIVATE;") >= 2
assert "ys_focus_accepts_direct_chars" in ui and "PostMessageW(focus, WM_CHAR" in ui
assert "ys_capture_external_target(state);\n    root = state->last_external_root;" in ui
assert "#define YS_EMOJI_BITMAP_CACHE_CAPACITY 384u" in emoji_renderer_c
assert "ys_warm_emoji_layout_cache" not in ui
assert "ys_emoji_renderer_warm_cache_async" not in emoji_renderer_h + emoji_renderer_c
assert "CreateThread(" not in emoji_renderer_c

# Existing native/DirectWrite implementation remains wired.
cmake = (R / "CMakeLists.txt").read_text(encoding="utf-8")
yesymbol_h = (R / "include/yesymbol.h").read_text(encoding="utf-8")
about_config = (R / "include/about_config.h").read_text(encoding="utf-8")
about_c = (R / "src/about.c").read_text(encoding="utf-8")
resource_rc = (R / "src/resource.rc").read_text(encoding="utf-8")
manifest = (R / "src/yesymbol.manifest").read_text(encoding="utf-8")
build_bat = (R / "build.bat").read_text(encoding="utf-8")
assert "LANGUAGES C CXX RC" in cmake
assert "set_source_files_properties(src/emoji_renderer.c PROPERTIES LANGUAGE CXX)" in cmake
assert "/MANIFEST:NO" in cmake
assert "d2d1" in cmake and "dwrite" in cmake
assert 'YESYMBOL_VERSION L"1.1.1"' in yesymbol_h
assert 'YESYMBOL_MODIFIED_DATE L"2026-08-20"' in about_config
assert 'YESYMBOL_UNICODE_LIST_URL L"https://unicode.org/emoji/charts/emoji-list.html"' in about_config
assert "修改日期：%s" in about_c and "Unicode List" in about_c
assert "FILEVERSION 1,1,1,0" in resource_rc and '"1.1.1\\0"' in resource_rc
assert 'assemblyIdentity version="1.1.1.0"' in manifest
assert "project(yesymbol VERSION 1.1.1" in cmake
run_section = build_bat.split(":run", 1)[1].split(":web", 1)[0]
assert 'call "%~f0" data' not in run_section


def assert_c_lexically_balanced(path: Path) -> None:
    text = path.read_text(encoding="ascii" if path.name == "symbol_data.c" else "utf-8")
    stack = []
    state = "normal"
    index = 0
    pairs = {")": "(", "]": "[", "}": "{"}
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "normal":
            if char == "/" and following == "/":
                state = "line-comment"; index += 2; continue
            if char == "/" and following == "*":
                state = "block-comment"; index += 2; continue
            if char == '"':
                state = "string"; index += 1; continue
            if char == "'":
                state = "character"; index += 1; continue
            if char in "([{":
                stack.append(char)
            elif char in ")]}":
                assert stack and stack[-1] == pairs[char], (path, char)
                stack.pop()
        elif state == "line-comment":
            if char == "\n": state = "normal"
        elif state == "block-comment":
            if char == "*" and following == "/":
                state = "normal"; index += 2; continue
        else:
            if char == "\\":
                index += 2; continue
            if (state == "string" and char == '"') or (state == "character" and char == "'"):
                state = "normal"
        index += 1
    assert state not in ("string", "character", "block-comment"), (path, state)
    assert not stack, (path, stack[-10:])


for source_path in list((R / "src").glob("*.c")) + list((R / "include").glob("*.h")):
    assert_c_lexically_balanced(source_path)

print("static checks passed")
