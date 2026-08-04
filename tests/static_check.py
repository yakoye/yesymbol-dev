from pathlib import Path
import importlib.util
import json
import subprocess
import sys
import tempfile

R = Path(__file__).resolve().parents[1]
D = json.loads((R / 'data-source/catalog.generated.json').read_text(encoding='utf-8'))
SERIES = json.loads((R / 'data-source/catalog-series-manifest.json').read_text(encoding='utf-8'))['series']

# Catalog integrity.
assert len(D['symbols']) == 16679
texts = [item['text'] for item in D['symbols']]
assert len(texts) == len(set(texts))
assert all(item.get('name_zh') and item.get('name_en') for item in D['symbols'])
by = {item['text']: item for item in D['symbols']}
assert by['♉']['name_zh'] == '金牛座'
assert by['♉']['name_en'].upper() == 'TAURUS'
assert by['♈']['name_zh'] == '白羊座'
for value in ['❶', '❿', '⓫', '⓴', '㉑', '㊿']:
    assert value in by

for category in D['categories']:
    seen_category = set()
    for group in category['groups']:
        items = group.get('items', [])
        assert len(items) == len(set(items)), (category['name'], group.get('title'))
        assert int(group.get('cols', 0) or 0) <= 12
        for symbol in items:
            assert symbol not in seen_category, (category['name'], symbol)
            seen_category.add(symbol)

# 全部符号 is generated as one unique union. Hidden Emoji variants remain searchable.
assert D['categories'][0]['name'] == '全部符号'
assert D['categories'][0].get('desc', '') == ''
all_items = [symbol for group in D['categories'][0]['groups'] for symbol in group.get('items', [])]
assert len(all_items) == len(set(all_items))
assert set(all_items).issubset(set(texts))
assert len(texts) > len(all_items)

# Exact requested UI hierarchy.
main = [
    '特殊符号', '标点符号', '序号字母', '数学/单位', '希腊/拉丁',
    '拼音/注音', '中文字符', '英文音标', '制表符',
    'Emoji·表情与人物', 'Emoji·动物与自然', 'Emoji·食物与活动',
    'Emoji·旅行与物品', 'Emoji·符号与旗帜',
]
other = ['日文字符', '韩文字符', '东亚字符', '大篆', '小篆', '俄文字符', '古埃及文字', '象形文字']
expected_ui = ['常用符号'] + main + ['其他符号'] + other + ['全部符号', '自定义']
assert D['ui_categories'] == expected_ui
assert D['ui_category_groups'] == {'其他符号': other}
static_names = [category['name'] for category in D['categories']]
for name in main + other + ['补充符号']:
    assert name in static_names
assert '数字序号' not in static_names
assert '字母序号' not in static_names
assert '东亚符号' not in static_names
assert '俄文字母' not in static_names
for seal in ['大篆', '小篆']:
    category = next(c for c in D['categories'] if c['name'] == seal)
    assert category['groups'] == []
    assert 'Unicode' in category.get('desc', '') and '字体' in category.get('desc', '')

# Text catalog stays hand-editable and derives rows/columns automatically.
converter = (R / 'tools/catalog_text.py').read_text(encoding='utf-8')
for token in ['yesymbol-catalog-text/2', 'command_export', 'command_build', 'command_check',
              'command_apply_cldr', 'command_dedupe', 'UI_MAIN_CATEGORIES', 'UI_OTHER_CATEGORIES',
              'normalize_visible_variant_key', 'orphan_symbols_placed']:
    assert token in converter
result = subprocess.run(
    [sys.executable, str(R / 'tools/catalog_text.py'), 'check'], cwd=R,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8')
assert result.returncode == 0, result.stdout
text_catalog = (R / 'data-source/catalog.txt').read_text(encoding='utf-8')
assert '@cols ' not in text_catalog
assert '@row ' not in text_catalog
assert '- ["' not in text_catalog
assert '@ui-categories ' not in text_catalog
assert '# 序号字母' in text_catalog
assert '## 字母序号' in text_catalog
assert '# 东亚字符' in text_catalog and '# 俄文字符' in text_catalog
assert '# 大篆' in text_catalog and '# 小篆' in text_catalog
assert '❶,❷,❸,❹,❺,❻,❼,❽,❾,❿' in text_catalog

regenerate = (R / 'regenerate-data.cmd').read_text(encoding='utf-8')
for token in ['update_cldr_zh.py ensure', 'catalog_text.py build', 'audit_catalog.py',
              'attrib -R "src\\symbol_data.c"', 'generate_bilingual_data.py']:
    assert token in regenerate


def logical_group(category_name, title):
    category = next(category for category in D['categories'] if category['name'] == category_name)
    values = []
    collecting = False
    for group in category['groups']:
        if group.get('spacer'):
            if collecting:
                break
            continue
        current_title = group.get('title', '')
        if current_title:
            if collecting:
                break
            collecting = current_title == title
        if collecting:
            values.extend(group.get('items', []))
    assert values, (category_name, title)
    return values


def codepoint_key(value):
    return tuple(ord(char) for char in value)

# Seven complete A-Z series now live inside 序号字母.
letter_expected = []
for start, end in [
    (0x24B6, 0x24D0), (0x24D0, 0x24EA), (0x249C, 0x24B6),
    (0x1F110, 0x1F12A), (0x1F130, 0x1F14A),
    (0x1F150, 0x1F16A), (0x1F170, 0x1F18A),
]:
    letter_expected.extend(map(chr, range(start, end)))
assert logical_group('序号字母', '字母序号') == letter_expected
circled_21_50 = logical_group('序号字母', '带圈数字 21–50')
assert len(circled_21_50) == 30 and circled_21_50[-1] == '㊿'

generated_unclassified = logical_group('补充符号', '未分类补充')
assert len(generated_unclassified) == 655
assert all(not by[value].get('emoji') for value in generated_unclassified)
assert set(generated_unclassified).issubset(set(all_items))

for title, key in {
    '麻将牌（Unicode 顺序）': 'mahjong',
    '多米诺骨牌（Unicode 顺序）': 'domino',
    '扑克牌（Unicode 顺序）': 'playing_cards',
    '国际象棋扩展符号（Unicode 顺序）': 'chess_symbols',
}.items():
    values = logical_group('特殊符号', title)
    assert values == [chr(int(cp, 16)) for cp in SERIES[key]]
    assert values == sorted(values, key=codepoint_key)

regional_flags = logical_group('Emoji·符号与旗帜', '国家和地区旗帜（Unicode 17.0）')
assert len(regional_flags) == 259
assert regional_flags == sorted(regional_flags, key=codepoint_key)
assert all(len(value) == 2 and all(0x1F1E6 <= ord(char) <= 0x1F1FF for char in value) for value in regional_flags)
assert '🇨🇶' in regional_flags
subdivision_flags = logical_group('Emoji·符号与旗帜', '地区旗帜（英格兰、苏格兰、威尔士）')
assert len(subdivision_flags) == 3
for flag in regional_flags + subdivision_flags:
    assert by[flag]['emoji'] and '旗' in by[flag]['name_zh']
assert by['🇨🇳']['name_zh'] == '中国国旗'
assert by['🇨🇶']['name_zh'] == '萨克岛国旗'

egyptian_items = [symbol for group in next(c for c in D['categories'] if c['name'] == '古埃及文字')['groups'] for symbol in group.get('items', [])]
assert len(egyptian_items) == 1110
assert '𓀀' in egyptian_items and chr(0x13455) in egyptian_items

# Parser convenience: no @row/@cols and long rows wrap automatically.
spec = importlib.util.spec_from_file_location('catalog_text_test', R / 'tools/catalog_text.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as temp_dir:
    sample = Path(temp_dir) / 'catalog.txt'
    sample.write_text(
        '@format yesymbol-catalog-text/2\n@default-common A\n#特殊符号\n'
        'A,B,C,D,E,F,G,H,I,J,K,L,M\n#@symbols\n'
        '@symbol A|字母A|LATIN CAPITAL LETTER A|false\n', encoding='utf-8')
    parsed = module.parse_text(sample)
    assert parsed['categories'][0]['name'] == '特殊符号'
    assert [len(group['items']) for group in parsed['categories'][0]['groups']] == [12, 1]

# Generated symbol lookup uses a build-time hash index rather than O(N) scans.
generator = (R / 'tools/generate_bilingual_data.py').read_text(encoding='utf-8')
generated_c = (R / 'src/symbol_data.c').read_text(encoding='ascii')
symbol_h = (R / 'include/symbol_data.h').read_text(encoding='utf-8')
for token in ['symbol_hash', 'hash_table', 'write_text_resilient', 'os.replace',
              'make_writable', 'PermissionError', 'unchanged; skipped rewrite',
              'origin_rows', 'origin_normalized_text', 'category_priorities']:
    assert token in generator
assert 'ys_symbol_index_from_text' in symbol_h
assert 'YSSymbolOriginRecord' in symbol_h and 'ys_symbol_origin' in symbol_h
assert 'g_ys_symbol_hash_table' in generated_c
assert 'int ys_symbol_index_from_text' in generated_c
assert 'const YSSymbolOriginRecord g_ys_symbol_origins[]' in generated_c

# TXT -> JSON -> C consistency and generated source-location records.
import re
origin_block = generated_c.split('const YSSymbolOriginRecord g_ys_symbol_origins[] = {', 1)[1].split('};', 1)[0]
origin_rows = [tuple(map(int, match)) for match in re.findall(r'\{(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u\}', origin_block)]
assert len(origin_rows) == len(D['symbols'])
assert all(row[0] != 65535 and row[1] > 0 and row[2] > 0 for row in origin_rows)
assert f'const size_t g_ys_symbol_count = {len(D["symbols"])}u;' in generated_c
category_index = {category['name']: index for index, category in enumerate(D['categories'])}
symbol_index = {item['text']: index for index, item in enumerate(D['symbols'])}
assert origin_rows[symbol_index['😤']][:3] == (category_index['Emoji·表情与人物'], 30, 10)
assert origin_rows[symbol_index['♉']][:3] == (category_index['特殊符号'], 10, 2)
assert origin_rows[symbol_index['𓀀']][:3] == (category_index['古埃及文字'], 1, 1)

# UI, hierarchy and performance paths.
ui = (R / 'src/ui.c').read_text(encoding='utf-8')
cfg = (R / 'include/ui_config.h').read_text(encoding='utf-8')
yesymbol_h = (R / 'include/yesymbol.h').read_text(encoding='utf-8')
for token in ['中文名称：%s', '英文名称：%s', 'ys_symbol_name_zh', 'ys_symbol_name_en']:
    assert token in ui
assert 'state->description' not in ui and 'ID_DESCRIPTION' not in ui
assert '全部内置符号的去重集合；可搜索、复制或右键添加到常用符号。' not in ui
assert '#define YS_WINDOW_WIDTH 786' in cfg
assert '#define YS_CATEGORY_WIDTH 162' in cfg
assert '#define YS_CATEGORY_ITEM_HEIGHT 28' in cfg
assert '#define YS_WINDOW_HEIGHT 650' in cfg
assert '#define YS_RECENT_MAX_VISIBLE 17' in cfg
assert '#define YS_COMMON_AUTO_ADD_THRESHOLD 5u' in cfg
assert 'YS_DESCRIPTION_HEIGHT' not in cfg

for token in [
    '最近使用 ▼', 'YESYMBOL_RECENT_CLASS', 'L"自动插入"', 'LBS_OWNERDRAWFIXED',
    'EM_SETCUEBANNER', 'ID_RECENT_CLEAR', 'ID_SEARCH_CLEAR',
    'ys_storage_save_bool(L"AutoInsert"', '手动添加自定义符号', 'SS_ETCHEDHORZ',
    'L"其他符号⯆"', 'L"其他符号⯈"',
]:
    assert token in ui or token in cfg or token in yesymbol_h
category_block = ui[ui.index('static void ys_add_category_items'):ui.index('static LRESULT CALLBACK ys_main_proc')]
assert category_block.index('ys_add_category_mapping_item(state, L"常用符号"') < category_block.index('for (i = 0; i < YS_ARRAY_COUNT(g_ys_main_category_names)')
assert category_block.index('L"全部符号"') < category_block.index('L"自定义"')
for name in main + other:
    assert f'L"{name}"' in ui
category_items_block = ui[ui.index('static void ys_add_category_items'):ui.index('static int ys_find_category_ui_index')]
assert 'L"补充符号"' not in category_items_block

# Performance: no fixed paste delay; target focus restoration, delayed writes,
# debounced search, deferred noncritical setup and visible-row virtualization.
for token in [
    'AttachThreadInput', 'GetGUIThreadInfo', 'SetForegroundWindow', 'SendInput',
    'YS_SEARCH_DEBOUNCE_MS 90u', 'YS_STORAGE_FLUSH_MS 350u',
    'ys_schedule_storage', 'YS_TIMER_STORAGE', 'YESYMBOL_DEFERRED_INIT_MESSAGE',
    'ys_create_tooltips', 'YS_VIRTUAL_THRESHOLD 480u', 'virtual_data_mode',
    'ys_build_virtual_category', 'ys_paint_virtual_data', 'ys_paint_virtual_flat',
    'ys_prepare_grid_backbuffer', 'ys_search_add_dynamic',
]:
    assert token in ui or token in yesymbol_h
assert 'Sleep(25)' not in ui
copy_start = ui.index('static void ys_copy_view')
copy_end = ui.index('static void ys_copy_hit', copy_start)
copy_block = ui[copy_start:copy_end]
assert copy_block.index('ys_send_paste(state)') < copy_block.index('ys_list_add_front_unique')
assert 'ys_storage_save_recent' not in copy_block and 'ys_storage_save_common' not in copy_block

storage_h = (R / 'include/storage.h').read_text(encoding='utf-8')
storage_c = (R / 'src/storage.c').read_text(encoding='utf-8')
assert 'ys_storage_load_bool' in storage_h and 'ys_storage_save_bool' in storage_h
assert 'YSSearchHistory' in storage_h and 'YS_MAX_SEARCH_HISTORY' in yesymbol_h
assert '#define _WIN32_IE 0x0600' in yesymbol_h
for token in ['SearchHistory', 'ys_storage_load_search_history', 'ys_storage_save_search_history',
              'ys_search_history_add_front', 'REG_MULTI_SZ']:
    assert token in storage_c or token in storage_h
assert 'REG_DWORD' in storage_c and 'AutoInsert' in ui

# Search-result metadata, original-location jump, history navigation and sidebar hover.
for token in [
    r'L"符号：%s\r\n中文名称：%s\r\n英文名称：%s\r\n分类：%s\r\n信息：%s\r\n编码：%s"',
    'ys_symbol_origin_valid', 'ys_format_hit_information', 'ys_origin_category_name',
    'ID_MENU_JUMP_ORIGIN', 'L"跳到所在位置"', 'ys_jump_to_symbol_origin',
    'YS_TIMER_SEARCH_HISTORY', 'YS_SEARCH_HISTORY_COMMIT_MS', 'VK_UP', 'VK_DOWN',
    'SetWindowSubclass', 'LB_ITEMFROMPOINT', 'category_hover_index', 'RGB(238, 244, 250)',
    'active_category_mapping', 'YS_CATEGORY_MAP_OTHER_HEADER',
]:
    assert token in ui or token in yesymbol_h
assert 'return L"搜索结果"' not in ui
assert 'SelectObject(draw->hDC, state->group_font)' not in ui[ui.index('case WM_DRAWITEM'):ui.index('case WM_COMMAND')]
assert 'state->other_expanded ? L"其他符号⯆" : L"其他符号⯈"' in ui
header_command = ui[ui.index('if (mapping == YS_CATEGORY_MAP_OTHER_HEADER)'):ui.index('if (selection >= 0)', ui.index('if (mapping == YS_CATEGORY_MAP_OTHER_HEADER)'))]
assert 'state->active_category_mapping = active_mapping' in header_command
assert 'LB_SETCURSEL, header_index' in header_command

# Build, tray, resources and about dialog remain intact.
assert '单击复制；右键可加入或移出常用符号' not in ui
for token in ['Shell_NotifyIconW', 'YESYMBOL_TRAY_MESSAGE', 'ID_TRAY_EXIT', 'SW_HIDE', 'ys_tray_add']:
    assert token in ui
assert 'WS_OVERLAPPEDWINDOW' not in ui and 'WS_MAXIMIZEBOX' not in ui
cmake = (R / 'CMakeLists.txt').read_text(encoding='utf-8')
assert '/MANIFEST:NO' in cmake and 'shell32' in cmake and 'src/about.c' in cmake
assert 'src/emoji_renderer.c' in cmake and 'd2d1' in cmake and 'dwrite' in cmake
assert 'LANGUAGES C CXX RC' in cmake
assert 'set_source_files_properties(src/emoji_renderer.c PROPERTIES LANGUAGE CXX)' in cmake
assert '$<$<COMPILE_LANGUAGE:CXX>:/GR->' in cmake
assert '1.0.3-rc1' in yesymbol_h
build = (R / 'build.bat').read_text(encoding='ascii')
for token in ['clean', 'data', 'cldr', 'run', 'all', 'call regenerate-data.cmd',
              'Reusing the existing CMake generator and platform', 'cmake -S . -B build -A x64']:
    assert token in build
assert not (R / 'build-clean.bat').exists() and not (R / 'autorun.bat').exists()

about = (R / 'src/about.c').read_text(encoding='utf-8')
resource = (R / 'src/resource.rc').read_text(encoding='utf-8')
assert (R / 'assets/yesymbol.ico').exists() and (R / 'assets/yesymbol-512.png').exists()
for token in ['TaskDialogIndirect', 'YESYMBOL_AUTHOR_EMAIL', 'YESYMBOL_DEVELOPMENT_PURPOSE', 'mailto:', 'github：', 'YESYMBOL_GITHUB_URL']:
    assert token in about
about_config = (R / 'include/about_config.h').read_text(encoding='utf-8')
assert 'https://github.com/yakoye/yesymbol-dev' in about_config and 'YESYMBOL_GITHUB_LABEL L"yesymbol-dev"' in about_config
for token in ['IDI_YESYMBOL', '关于 YeSymbol', 'ID_TRAY_ABOUT', 'ID_SYSTEM_ABOUT']:
    assert token in ui or token in resource
assert 'FILEVERSION 1,0,3,0' in resource and 'PRODUCTVERSION 1,0,3,0' in resource

# DirectWrite/Direct2D color Emoji and fixed 12-column common layout.
emoji_renderer = (R / 'src/emoji_renderer.c').read_text(encoding='utf-8')
emoji_header = (R / 'include/emoji_renderer.h').read_text(encoding='utf-8')
assert '#define CINTERFACE' in emoji_renderer and '#define COBJMACROS' in emoji_renderer
assert 'extern "C"' in emoji_header
for token in ['D2D1CreateFactory', 'DWriteCreateFactory', 'Segoe UI Emoji',
              'D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT', 'ID2D1DCRenderTarget_DrawText',
              'ys_emoji_renderer_begin', 'ys_emoji_renderer_end']:
    assert token in emoji_renderer or token in emoji_header
assert 'YSEmojiDrawBatch' in ui and 'ys_render_emoji_batch' in ui
assert 'ys_layout_symbol_range_fit_columns(state, first, state->view_count - first, 12' in ui

# v1.0.2: fixed common order, auto-add threshold, persistent usage counts and drag sorting.
for token in [
    'YS_STORAGE_DIRTY_USAGE', 'YS_COMMON_AUTO_ADD_THRESHOLD', 'ys_record_symbol_use',
    'ys_usage_increment', 'ys_usage_get', 'ys_storage_load_usage', 'ys_storage_save_usage',
    'ys_common_move', 'common_drag_source', 'common_drag_target', 'SetCapture(hwnd)',
    'IDC_SIZEALL', 'L"常用符号（拖动排序，右键删除）"',
    'L"该符号已达到常用阈值，并追加到常用符号末尾。"',
]:
    assert token in ui or token in storage_h or token in storage_c or token in cfg
assert 'ys_common_sort' not in storage_h and 'ys_common_sort' not in storage_c
assert 'CommonV2' in storage_c and 'UsageV1' in storage_c
assert 'ys_load_common_value(key, L"CommonV1", 1u, list)' in storage_c
assert 'ys_recent_item_rect(const YSAppState *state, size_t index)' in ui
assert 'YS_RECENT_CELL_WIDTH' not in cfg

# Pinned CLDR cache and audit.
audit = (R / 'tools/audit_catalog.py').read_text(encoding='utf-8')
for token in ['259 regional flags', '带圈数字 21–50', '古埃及文字', '大篆', '小篆']:
    assert token in audit
cldr_tool = (R / 'tools/update_cldr_zh.py').read_text(encoding='utf-8')
for token in ['c9a5503bf238114a1993377b87841fb76031371d',
              '765c963b2ba18ce5844bc737b1d4570b0b45648e', 'type=tts', 'U+FE0F', 'urllib.request']:
    assert token in cldr_tool
cldr_cache = json.loads((R / 'data-source/cldr-annotations-zh.tts.json').read_text(encoding='utf-8'))
assert cldr_cache['metadata']['commit'] == 'c9a5503bf238114a1993377b87841fb76031371d'
assert cldr_cache['tts']['↤'] == '尾部带杠的向左箭头'
assert module.normalize_cldr_key('☹️') == '☹'




def assert_c_lexically_balanced(path: Path) -> None:
    text = path.read_text(encoding='ascii' if path.name == 'symbol_data.c' else 'utf-8')
    stack = []
    state = 'normal'
    index = 0
    line = 1
    pairs = {')': '(', ']': '[', '}': '{'}
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ''
        if char == '\n':
            line += 1
        if state == 'normal':
            if char == '/' and following == '/':
                state = 'line-comment'
                index += 2
                continue
            if char == '/' and following == '*':
                state = 'block-comment'
                index += 2
                continue
            if char == '"':
                state = 'string'
                index += 1
                continue
            if char == "'":
                state = 'character'
                index += 1
                continue
            if char in '([{':
                stack.append((char, line))
            elif char in ')]}':
                assert stack and stack[-1][0] == pairs[char], (path, line, char, stack[-3:])
                stack.pop()
        elif state == 'line-comment':
            if char == '\n':
                state = 'normal'
        elif state == 'block-comment':
            if char == '*' and following == '/':
                state = 'normal'
                index += 2
                continue
        elif state in ('string', 'character'):
            if char == '\\':
                index += 2
                continue
            if (state == 'string' and char == '"') or (state == 'character' and char == "'"):
                state = 'normal'
        index += 1
    assert state not in ('string', 'character', 'block-comment'), (path, state)
    assert not stack, (path, stack[-10:])


for source_path in list((R / 'src').glob('*.c')) + list((R / 'include').glob('*.h')):
    assert_c_lexically_balanced(source_path)

# README covers the complete user/developer workflow and v1.0.3-rc1 behavior.
readme = (R / 'README.md').read_text(encoding='utf-8')
for heading in ['## 项目介绍', '## 开发目的', '## 功能特点', '## 快速开始',
                '### 直接运行', '### 从源码编译运行', '#### 编译环境要求',
                '#### 一键清理、编译并运行', '## 使用方法', '### 复制符号',
                '### 搜索', '### 最近使用、常用和自定义', '## 高级功能',
                '### 1. 界面参数调整', '### 2. 数据维护（可以增加、删除、重排符号）',
                '#### 2.1 刷新官方 CLDR 中文短名称并把适合替换的名称写回 catalog.txt',
                '#### 2.2 人工修改 `data-source\\catalog.txt`', '## 技术说明',
                '## 已知限制', '## 许可证']:
    assert heading in readme
for token in ['catalog.txt', 'catalog.generated.json', r'src\symbol_data.c',
              '运行时不会打开或解析JSON', r'.\build.bat run',
              r'python tools\catalog_text.py build', r'python tests\static_check.py',
              '#define YS_WINDOW_WIDTH 786', '#define YS_CATEGORY_WIDTH 162',
              'DirectWrite + Direct2D', 'D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT',
              '常用符号固定每行显示 12 个', '最多显示 17 个符号', '拖动',
              'YS_COMMON_AUTO_ADD_THRESHOLD 5u', 'CommonV2', 'UsageV1',
              'https://github.com/yakoye/yesymbol-dev']:
    assert token in readme

print('static checks passed')
