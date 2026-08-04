#include "ui.h"
#include "about.h"
#include "resource.h"
#include "clipboard.h"
#include "storage.h"
#include "symbol_data.h"
#include "emoji_renderer.h"
#include "ui_config.h"
#include <commctrl.h>
#include <windowsx.h>
#include <strsafe.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>
#include <wctype.h>

#define ID_SEARCH 1001
#define ID_SEARCH_CLEAR 1002
#define ID_CATEGORIES 1003
#define ID_GRID 1004
#define ID_AUTO_INSERT 1005
#define ID_TOPMOST 1006
#define ID_CUSTOM_TEXT 1007
#define ID_ADD_CUSTOM 1008
#define ID_STATUS 1010
#define ID_RECENT_TOGGLE 1011
#define ID_RECENT_CLEAR 1012
#define ID_RECENT_GRID 1013
#define ID_FOOTER_SEPARATOR 1014
#define ID_TRAY_OPEN 2201
#define ID_TRAY_ABOUT 2202
#define ID_TRAY_EXIT 2203
#define ID_SYSTEM_ABOUT 0x1F00u
#define YS_TRAY_ICON_ID 1u

#define YS_ITEM_HEADER 1u
#define YS_ITEM_SYMBOL 2u
#define YS_SOURCE_DATA 0u
#define YS_SOURCE_RECENT 1u
#define YS_SOURCE_COMMON 2u
#define YS_SOURCE_CUSTOM 3u

#define YS_CELL_W YS_CELL_WIDTH
#define YS_CELL_H YS_CELL_HEIGHT
#define YS_HEADER_H 27
#define YS_GROUP_GAP 8
#define YS_ROW_GAP 10

#define YS_CATEGORY_MAP_COMMON (-1)
#define YS_CATEGORY_MAP_CUSTOM (-2)
#define YS_CATEGORY_MAP_OTHER_HEADER (-3)

#define YS_TIMER_FOREGROUND 1u
#define YS_TIMER_SEARCH 2u
#define YS_TIMER_STORAGE 3u
#define YS_TIMER_SEARCH_HISTORY 4u
#define YS_SEARCH_DEBOUNCE_MS 90u
#define YS_STORAGE_FLUSH_MS 350u
#define YS_SEARCH_HISTORY_COMMIT_MS 650u
#define YS_FOREGROUND_POLL_MS 150u
#define YS_STORAGE_DIRTY_RECENT 0x01u
#define YS_STORAGE_DIRTY_COMMON 0x02u
#define YS_STORAGE_DIRTY_CUSTOM 0x04u
#define YS_STORAGE_DIRTY_SEARCH_HISTORY 0x08u
#define YS_STORAGE_DIRTY_USAGE 0x10u
#define YS_VIRTUAL_THRESHOLD 480u
#define ID_MENU_ADD_COMMON 2101
#define ID_MENU_REMOVE_COMMON 2102
#define ID_MENU_REMOVE_RECENT 2103
#define ID_MENU_REMOVE_CUSTOM 2104
#define ID_MENU_JUMP_ORIGIN 2105
#define ID_MENU_TONE_BASE 2300
#define YS_MAX_TONE_OPTIONS 6
#define YS_MAX_VISIBLE_EMOJI_DRAWS 512u

static const WCHAR *g_ys_main_category_names[] = {
    L"特殊符号", L"标点符号", L"序号字母", L"数学/单位", L"希腊/拉丁",
    L"拼音/注音", L"中文字符", L"英文音标", L"制表符",
    L"Emoji·表情与人物", L"Emoji·动物与自然", L"Emoji·食物与活动",
    L"Emoji·旅行与物品", L"Emoji·符号与旗帜"
};

static const WCHAR *g_ys_other_category_names[] = {
    L"日文字符", L"韩文字符", L"东亚字符", L"大篆", L"小篆",
    L"俄文字符", L"古埃及文字", L"象形文字"
};

typedef struct YSViewSymbol {
    const WCHAR *text;
    const WCHAR *name_zh;
    const WCHAR *name_en;
    uint32_t flags;
    uint32_t symbol_index;
    uint16_t source;
    uint16_t source_index;
} YSViewSymbol;

typedef struct YSLayoutItem {
    RECT rect;
    const WCHAR *header;
    uint32_t view_index;
    uint16_t category_index;
    uint16_t row_number;
    uint16_t column_number;
    uint8_t type;
} YSLayoutItem;

typedef struct YSVirtualGroup {
    uint32_t group_index;
    int header_top;
    int symbols_top;
    int bottom;
    uint16_t columns;
    uint16_t rows;
} YSVirtualGroup;

typedef struct YSHitInfo {
    BOOL valid;
    BOOL virtual_item;
    int layout_index;
    int virtual_group_index;
    uint32_t virtual_item_index;
    uint32_t view_index;
    uint32_t symbol_index;
    uint16_t category_index;
    uint16_t row_number;
    uint16_t column_number;
} YSHitInfo;

typedef struct YSEmojiDrawItem {
    WCHAR text[YS_MAX_SEQUENCE + 4];
    RECT rect;
    COLORREF fallback_color;
} YSEmojiDrawItem;

typedef struct YSEmojiDrawBatch {
    YSEmojiDrawItem items[YS_MAX_VISIBLE_EMOJI_DRAWS];
    size_t count;
} YSEmojiDrawBatch;

typedef struct YSAppState {
    HINSTANCE instance;
    HWND hwnd;
    HWND recent_toggle;
    HWND search;
    HWND search_clear;
    HWND auto_insert;
    HWND topmost;
    HWND recent_grid;
    HWND recent_clear;
    HWND categories;
    HWND grid;
    HWND custom_label;
    HWND custom_text;
    HWND add_custom;
    HWND footer_separator;
    HWND status;
    HWND tooltip;
    HWND recent_tooltip;
    HWND last_external_root;
    HWND last_external_focus;
    DWORD last_external_thread;
    NOTIFYICONDATAW tray_icon;
    UINT taskbar_created_message;
    BOOL tray_added;
    BOOL exiting;
    HFONT ui_font;
    HFONT group_font;
    HFONT symbol_font;
    HFONT emoji_font;
    YSEmojiRenderer *emoji_renderer;
    YSDynamicList recent;
    YSCommonList common;
    YSUsageList usage;
    YSDynamicList custom;
    YSSearchHistory search_history;
    YSViewSymbol *views;
    size_t view_count;
    size_t view_capacity;
    YSLayoutItem *layout;
    size_t layout_count;
    size_t layout_capacity;
    YSVirtualGroup *virtual_groups;
    size_t virtual_group_count;
    size_t virtual_group_capacity;
    BOOL virtual_data_mode;
    BOOL virtual_flat_mode;
    int virtual_category_index;
    int virtual_flat_top;
    int virtual_flat_columns;
    int selected_ui;
    int active_category_mapping;
    int *category_map;
    size_t category_item_count;
    int custom_ui_index;
    int all_symbols_ui_index;
    int scroll_y;
    int content_height;
    int hover_layout;
    int hover_virtual_group;
    int hover_virtual_item;
    int recent_hover_index;
    int category_hover_index;
    int search_history_nav;
    BOOL recent_expanded;
    BOOL other_expanded;
    BOOL suppress_search_change;
    BOOL pending_common_rebuild;
    int common_drag_source;
    int common_drag_target;
    POINT common_drag_start;
    BOOL common_dragging;
    UINT storage_dirty_flags;
    HDC grid_memory_dc;
    HBITMAP grid_bitmap;
    HBITMAP grid_old_bitmap;
    int grid_buffer_width;
    int grid_buffer_height;
    WCHAR search_header[96];
    WCHAR search_history_draft[YS_MAX_QUERY];
    WCHAR tooltip_text[1280];
} YSAppState;

static void ys_jump_to_symbol_origin(YSAppState *state, uint32_t symbol_index);
static void ys_layout_controls(YSAppState *state);
static LRESULT CALLBACK ys_search_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                UINT_PTR subclass_id, DWORD_PTR reference_data);
static LRESULT CALLBACK ys_categories_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                    UINT_PTR subclass_id, DWORD_PTR reference_data);

static BOOL ys_reserve(void **memory, size_t *capacity, size_t required, size_t item_size) {
    size_t next;
    void *result;
    if (required <= *capacity) return TRUE;
    next = *capacity ? *capacity : 256u;
    while (next < required) {
        if (next > ((size_t)-1) / 2u) return FALSE;
        next *= 2u;
    }
    if (next > ((size_t)-1) / item_size) return FALSE;
    if (*memory) result = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *memory, next * item_size);
    else result = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, next * item_size);
    if (!result) return FALSE;
    *memory = result;
    *capacity = next;
    return TRUE;
}

static void ys_set_font(HWND hwnd, HFONT font) {
    if (hwnd && font) SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

static HFONT ys_create_ui_font(void) {
    NONCLIENTMETRICSW metrics;
    ZeroMemory(&metrics, sizeof(metrics));
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static BOOL ys_is_high_surrogate(WCHAR c) { return c >= 0xD800 && c <= 0xDBFF; }
static BOOL ys_is_low_surrogate(WCHAR c) { return c >= 0xDC00 && c <= 0xDFFF; }

static uint32_t ys_decode_codepoint(const WCHAR *text, size_t length, size_t *index) {
    uint32_t first;
    if (*index >= length) return 0;
    first = text[(*index)++];
    if (ys_is_high_surrogate((WCHAR)first) && *index < length && ys_is_low_surrogate(text[*index])) {
        uint32_t second = text[(*index)++];
        return 0x10000u + ((first - 0xD800u) << 10u) + (second - 0xDC00u);
    }
    return first;
}

static BOOL ys_text_looks_emoji(const WCHAR *text) {
    size_t i = 0, length = wcslen(text);
    while (i < length) {
        uint32_t cp = ys_decode_codepoint(text, length, &i);
        if (cp >= 0x1F000u || cp == 0x200Du || cp == 0xFE0Fu || (cp >= 0x1F3FBu && cp <= 0x1F3FFu)) return TRUE;
    }
    return FALSE;
}

static void ys_format_codepoints(const WCHAR *text, WCHAR *output, size_t capacity) {
    size_t i = 0, length = wcslen(text), used = 0;
    output[0] = 0;
    while (i < length && used + 12u < capacity) {
        uint32_t cp = ys_decode_codepoint(text, length, &i);
        WCHAR part[16];
        if (cp <= 0xFFFFu) StringCchPrintfW(part, YS_ARRAY_COUNT(part), L"U+%04X", cp);
        else StringCchPrintfW(part, YS_ARRAY_COUNT(part), L"U+%X", cp);
        if (used) {
            StringCchCatW(output, capacity, L" ");
            ++used;
        }
        StringCchCatW(output, capacity, part);
        used = wcslen(output);
    }
}

static BOOL ys_is_combining_codepoint(uint32_t cp) {
    return (cp >= 0x0300u && cp <= 0x036Fu) || (cp >= 0x0483u && cp <= 0x0489u) ||
           (cp >= 0x0591u && cp <= 0x05BDu) || (cp >= 0x0610u && cp <= 0x061Au) ||
           (cp >= 0x064Bu && cp <= 0x065Fu) || (cp >= 0x0B82u && cp <= 0x0B82u) ||
           (cp >= 0x0BBEu && cp <= 0x0BC2u) || (cp >= 0x0E31u && cp <= 0x0E4Eu);
}

static const WCHAR *ys_display_text(const WCHAR *text, WCHAR *buffer, size_t capacity) {
    size_t index = 0;
    uint32_t cp;
    if (!text || !text[0]) return L"";
    cp = ys_decode_codepoint(text, wcslen(text), &index);
    if (text[1] == 0 && cp == 0x20u) return L"␠";
    if (text[1] == 0 && cp == 0x3000u) return L"□";
    if (text[1] == 0 && cp == 0x00ADu) return L"SHY";
    if (ys_is_combining_codepoint(cp)) {
        StringCchPrintfW(buffer, capacity, L"◌%s", text);
        return buffer;
    }
    return text;
}

static void ys_hide_tooltip(YSAppState *state) {
    TOOLINFOW info;
    if (!state || !state->tooltip || !state->grid) return;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.hwnd = state->grid;
    info.uId = 1u;
    SendMessageW(state->tooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&info);
}

static BOOL ys_hit_view(const YSAppState *state, const YSHitInfo *hit, YSViewSymbol *view) {
    const YSSymbolRecord *record;
    if (!state || !hit || !hit->valid || !view) return FALSE;
    if (hit->virtual_item && state->virtual_data_mode) {
        if (hit->symbol_index >= g_ys_symbol_count) return FALSE;
        record = &g_ys_symbols[hit->symbol_index];
        ZeroMemory(view, sizeof(*view));
        view->text = ys_symbol_text(hit->symbol_index);
        view->name_zh = ys_symbol_name_zh(hit->symbol_index);
        view->name_en = ys_symbol_name_en(hit->symbol_index);
        view->flags = record->flags;
        view->symbol_index = hit->symbol_index;
        view->source = YS_SOURCE_DATA;
        return TRUE;
    }
    if (hit->virtual_item && state->virtual_flat_mode) {
        if (hit->view_index >= state->view_count) return FALSE;
        *view = state->views[hit->view_index];
        return TRUE;
    }
    if (hit->layout_index < 0 || (size_t)hit->layout_index >= state->layout_count) return FALSE;
    if (state->layout[hit->layout_index].type != YS_ITEM_SYMBOL) return FALSE;
    if (state->layout[hit->layout_index].view_index >= state->view_count) return FALSE;
    *view = state->views[state->layout[hit->layout_index].view_index];
    return TRUE;
}

static BOOL ys_symbol_origin_valid(uint32_t symbol_index) {
    const YSSymbolOriginRecord *origin;
    if (symbol_index >= g_ys_symbol_count) return FALSE;
    origin = ys_symbol_origin(symbol_index);
    return origin->category_index < g_ys_category_count &&
           origin->group_index < g_ys_group_count &&
           origin->row_number > 0 && origin->column_number > 0;
}

static const WCHAR *ys_origin_category_name(uint32_t symbol_index) {
    const YSSymbolOriginRecord *origin;
    if (!ys_symbol_origin_valid(symbol_index)) return NULL;
    origin = ys_symbol_origin(symbol_index);
    return ys_pool_string(g_ys_categories[origin->category_index].name_offset);
}

static BOOL ys_search_is_active(const YSAppState *state) {
    return state && state->search && GetWindowTextLengthW(state->search) > 0;
}

static const WCHAR *ys_hit_category_name(const YSAppState *state, const YSHitInfo *hit, const YSViewSymbol *view) {
    const WCHAR *origin_name;
    if (ys_search_is_active(state) && view && view->symbol_index < g_ys_symbol_count) {
        origin_name = ys_origin_category_name(view->symbol_index);
        if (origin_name) return origin_name;
    }
    if (hit && hit->category_index < g_ys_category_count) {
        return ys_pool_string(g_ys_categories[hit->category_index].name_offset);
    }
    if (view) {
        if (view->source == YS_SOURCE_RECENT) return L"最近使用";
        if (view->source == YS_SOURCE_COMMON) return L"常用符号";
        if (view->source == YS_SOURCE_CUSTOM) return L"自定义";
        origin_name = ys_origin_category_name(view->symbol_index);
        if (origin_name) return origin_name;
    }
    return L"未分类";
}

static void ys_format_hit_information(const YSAppState *state, const YSHitInfo *hit,
                                      const YSViewSymbol *view, WCHAR *output, size_t capacity) {
    const YSSymbolOriginRecord *origin;
    if (!output || !capacity) return;
    output[0] = 0;
    if (ys_search_is_active(state) && view && ys_symbol_origin_valid(view->symbol_index)) {
        origin = ys_symbol_origin(view->symbol_index);
        StringCchPrintfW(output, capacity, L"第 %u 行，第 %u 个",
                         (unsigned)origin->row_number, (unsigned)origin->column_number);
        return;
    }
    if (hit && hit->category_index < g_ys_category_count && hit->row_number && hit->column_number) {
        StringCchPrintfW(output, capacity, L"第 %u 行，第 %u 个",
                         (unsigned)hit->row_number, (unsigned)hit->column_number);
        return;
    }
    if (view && view->source == YS_SOURCE_COMMON && view->source_index < state->common.count) {
        StringCchPrintfW(output, capacity, L"已使用 %u 次",
                         state->common.items[view->source_index].use_count);
        return;
    }
    if (view && view->source == YS_SOURCE_RECENT) {
        StringCchCopyW(output, capacity, L"最近使用记录");
        return;
    }
    if (view && view->source == YS_SOURCE_CUSTOM) {
        StringCchCopyW(output, capacity, L"用户自定义符号");
        return;
    }
    StringCchCopyW(output, capacity, L"暂无位置信息");
}

static void ys_show_tooltip_hit(YSAppState *state, const YSHitInfo *hit) {
    TOOLINFOW info;
    POINT point;
    YSViewSymbol view;
    WCHAR codes[192];
    const WCHAR *category;
    WCHAR position[128];
    if (!state || !state->tooltip || !ys_hit_view(state, hit, &view)) return;
    category = ys_hit_category_name(state, hit, &view);
    ys_format_hit_information(state, hit, &view, position, YS_ARRAY_COUNT(position));
    ys_format_codepoints(view.text, codes, YS_ARRAY_COUNT(codes));
    StringCchPrintfW(state->tooltip_text, YS_ARRAY_COUNT(state->tooltip_text),
                     L"符号：%s\r\n中文名称：%s\r\n英文名称：%s\r\n分类：%s\r\n信息：%s\r\n编码：%s",
                     view.text, view.name_zh ? view.name_zh : L"", view.name_en ? view.name_en : L"",
                     category, position, codes);
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.hwnd = state->grid;
    info.uId = 1u;
    info.lpszText = state->tooltip_text;
    SendMessageW(state->tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&info);
    GetCursorPos(&point);
    SendMessageW(state->tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(point.x + 14, point.y + 18));
    SendMessageW(state->tooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&info);
}

static const WCHAR *ys_category_name_for_view(const YSViewSymbol *view) {
    const WCHAR *origin_name;
    if (!view) return L"";
    if (view->source == YS_SOURCE_RECENT) return L"最近使用";
    if (view->source == YS_SOURCE_COMMON) return L"常用符号";
    if (view->source == YS_SOURCE_CUSTOM) return L"自定义";
    origin_name = ys_origin_category_name(view->symbol_index);
    return origin_name ? origin_name : L"未分类";
}

static void ys_set_footer(YSAppState *state, const YSViewSymbol *view, const WCHAR *category, const WCHAR *prefix) {
    WCHAR codes[192], message[768];
    if (!state || !state->status || !view) return;
    ys_format_codepoints(view->text, codes, YS_ARRAY_COUNT(codes));
    StringCchPrintfW(message, YS_ARRAY_COUNT(message), L"%s%s   %s   %s   %s   %s",
                     prefix ? prefix : L"", view->text,
                     view->name_zh ? view->name_zh : L"", codes,
                     view->name_en ? view->name_en : L"", category ? category : ys_category_name_for_view(view));
    SetWindowTextW(state->status, message);
}

static void ys_set_status_for_view(YSAppState *state, const YSViewSymbol *view, const WCHAR *prefix) {
    ys_set_footer(state, view, ys_category_name_for_view(view), prefix);
}

static void ys_set_status_for_layout(YSAppState *state, const YSLayoutItem *item, const YSViewSymbol *view, const WCHAR *prefix) {
    const WCHAR *category = ys_category_name_for_view(view);
    if (item && item->category_index < g_ys_category_count) category = ys_pool_string(g_ys_categories[item->category_index].name_offset);
    ys_set_footer(state, view, category, prefix);
}

static size_t ys_append_view(YSAppState *state, const WCHAR *text, const WCHAR *name_zh, const WCHAR *name_en, uint32_t flags,
                             uint32_t symbol_index, uint16_t source, uint16_t source_index) {
    YSViewSymbol *view;
    if (!ys_reserve((void **)&state->views, &state->view_capacity, state->view_count + 1u, sizeof(*state->views))) return (size_t)-1;
    view = &state->views[state->view_count];
    view->text = text;
    view->name_zh = name_zh;
    view->name_en = name_en;
    view->flags = flags;
    view->symbol_index = symbol_index;
    view->source = source;
    view->source_index = source_index;
    return state->view_count++;
}

static BOOL ys_append_layout(YSAppState *state, const YSLayoutItem *item) {
    if (!ys_reserve((void **)&state->layout, &state->layout_capacity, state->layout_count + 1u, sizeof(*state->layout))) return FALSE;
    state->layout[state->layout_count++] = *item;
    return TRUE;
}

static int ys_grid_width(YSAppState *state) {
    RECT client;
    if (!state->grid) return 520;
    GetClientRect(state->grid, &client);
    return max(100, client.right - client.left);
}

static void ys_layout_group_begin(YSAppState *state, const WCHAR *title, int *y) {
    YSLayoutItem item;
    ZeroMemory(&item, sizeof(item));
    item.type = YS_ITEM_HEADER;
    item.header = title;
    item.rect.left = 4;
    item.rect.top = *y;
    item.rect.right = ys_grid_width(state) - 4;
    item.rect.bottom = *y + YS_HEADER_H;
    ys_append_layout(state, &item);
    *y += YS_HEADER_H + 3;
}

static void ys_layout_symbol_range(YSAppState *state, size_t first_view, size_t count, uint16_t preferred_columns, int *y,
                                   uint16_t category_index, uint16_t row_number) {
    int dynamic_columns = max(1, min(YS_MAX_COLUMNS, (ys_grid_width(state) - 8) / YS_CELL_W));
    int columns = preferred_columns ? min(YS_MAX_COLUMNS, (int)preferred_columns) : dynamic_columns;
    size_t i;
    int rows;
    for (i = 0; i < count; ++i) {
        int row = (int)(i / (size_t)columns);
        int column = (int)(i % (size_t)columns);
        YSLayoutItem item;
        ZeroMemory(&item, sizeof(item));
        item.type = YS_ITEM_SYMBOL;
        item.view_index = (uint32_t)(first_view + i);
        item.category_index = category_index;
        item.row_number = row_number;
        item.column_number = (uint16_t)(i + 1u);
        item.rect.left = 4 + column * YS_CELL_W;
        item.rect.top = *y + row * YS_CELL_H;
        item.rect.right = item.rect.left + YS_CELL_W - 2;
        item.rect.bottom = item.rect.top + YS_CELL_H - 2;
        ys_append_layout(state, &item);
    }
    rows = count ? (int)((count + (size_t)columns - 1u) / (size_t)columns) : 0;
    *y += rows * YS_CELL_H + YS_GROUP_GAP;
}

static void ys_layout_symbol_range_fit_columns(YSAppState *state, size_t first_view, size_t count,
                                               int requested_columns, int *y,
                                               uint16_t category_index, uint16_t row_number) {
    int columns = max(1, min(YS_MAX_COLUMNS, requested_columns));
    int available_width = max(columns, ys_grid_width(state) - 8);
    int cell_width = max(24, available_width / columns);
    size_t i;
    int rows;
    for (i = 0; i < count; ++i) {
        int row = (int)(i / (size_t)columns);
        int column = (int)(i % (size_t)columns);
        YSLayoutItem item;
        ZeroMemory(&item, sizeof(item));
        item.type = YS_ITEM_SYMBOL;
        item.view_index = (uint32_t)(first_view + i);
        item.category_index = category_index;
        item.row_number = row_number;
        item.column_number = (uint16_t)(i + 1u);
        item.rect.left = 4 + column * cell_width;
        item.rect.top = *y + row * YS_CELL_H;
        item.rect.right = (column == columns - 1) ? ys_grid_width(state) - 4 : item.rect.left + cell_width - 2;
        item.rect.bottom = item.rect.top + YS_CELL_H - 2;
        ys_append_layout(state, &item);
    }
    rows = count ? (int)((count + (size_t)columns - 1u) / (size_t)columns) : 0;
    *y += rows * YS_CELL_H + YS_GROUP_GAP;
}

static void ys_reset_virtual_layout(YSAppState *state) {
    if (!state) return;
    state->virtual_group_count = 0;
    state->virtual_data_mode = FALSE;
    state->virtual_flat_mode = FALSE;
    state->virtual_category_index = -1;
    state->virtual_flat_top = 0;
    state->virtual_flat_columns = 0;
    state->hover_virtual_group = -1;
    state->hover_virtual_item = -1;
}

static size_t ys_category_symbol_count(int data_index) {
    const YSCategoryRecord *category;
    uint32_t i;
    size_t total = 0;
    if (data_index < 0 || (size_t)data_index >= g_ys_category_count) return 0;
    category = &g_ys_categories[data_index];
    for (i = 0; i < category->group_count; ++i) {
        const YSGroupRecord *group = &g_ys_groups[category->first_group + i];
        if (!(group->flags & YS_GROUP_FLAG_SPACER)) total += group->item_count;
    }
    return total;
}

static BOOL ys_append_virtual_group(YSAppState *state, const YSVirtualGroup *group) {
    if (!ys_reserve((void **)&state->virtual_groups, &state->virtual_group_capacity,
                    state->virtual_group_count + 1u, sizeof(*state->virtual_groups))) return FALSE;
    state->virtual_groups[state->virtual_group_count++] = *group;
    return TRUE;
}

static void ys_build_virtual_category(YSAppState *state, int data_index, int *y) {
    const YSCategoryRecord *category = &g_ys_categories[data_index];
    uint32_t i;
    int dynamic_columns = max(1, min(YS_MAX_COLUMNS, (ys_grid_width(state) - 8) / YS_CELL_W));
    state->virtual_data_mode = TRUE;
    state->virtual_category_index = data_index;
    for (i = 0; i < category->group_count; ++i) {
        uint32_t group_index = category->first_group + i;
        const YSGroupRecord *record = &g_ys_groups[group_index];
        YSVirtualGroup group;
        int columns, rows;
        if (record->flags & YS_GROUP_FLAG_SPACER) {
            *y += YS_ROW_GAP;
            continue;
        }
        ZeroMemory(&group, sizeof(group));
        group.group_index = group_index;
        group.header_top = -1;
        if (ys_pool_string(record->title_offset)[0]) {
            group.header_top = *y;
            *y += YS_HEADER_H + 3;
        }
        columns = record->preferred_columns ? min(YS_MAX_COLUMNS, (int)record->preferred_columns) : dynamic_columns;
        rows = record->item_count ? (int)((record->item_count + (uint32_t)columns - 1u) / (uint32_t)columns) : 0;
        group.columns = (uint16_t)max(1, columns);
        group.rows = (uint16_t)max(0, rows);
        group.symbols_top = *y;
        group.bottom = *y + rows * YS_CELL_H + YS_GROUP_GAP;
        *y = group.bottom;
        ys_append_virtual_group(state, &group);
    }
}

static void ys_layout_virtual_flat(YSAppState *state, int *y) {
    int columns = max(1, min(YS_MAX_COLUMNS, (ys_grid_width(state) - 8) / YS_CELL_W));
    int rows = state->view_count ? (int)((state->view_count + (size_t)columns - 1u) / (size_t)columns) : 0;
    state->virtual_flat_mode = TRUE;
    state->virtual_flat_top = *y;
    state->virtual_flat_columns = columns;
    *y += rows * YS_CELL_H + YS_GROUP_GAP;
}

static int ys_category_map_value(const YSAppState *state, int ui_index) {
    if (!state || !state->category_map || ui_index < 0 || (size_t)ui_index >= state->category_item_count) return YS_CATEGORY_MAP_COMMON;
    return state->category_map[ui_index];
}

static BOOL ys_selected_is_common(const YSAppState *state) {
    return state && state->active_category_mapping == YS_CATEGORY_MAP_COMMON;
}

static BOOL ys_selected_is_custom(const YSAppState *state) {
    return state && state->active_category_mapping == YS_CATEGORY_MAP_CUSTOM;
}

static int ys_data_category_from_ui(const YSAppState *state, int ui_index) {
    int value = ys_category_map_value(state, ui_index);
    return value >= 0 && (size_t)value < g_ys_category_count ? value : -1;
}

static BOOL ys_query_hex(const WCHAR *query, WCHAR *normalized, size_t capacity) {
    size_t i, j = 0;
    BOOL any = FALSE;
    for (i = 0; query[i] && j + 1u < capacity; ++i) {
        WCHAR c = query[i];
        if (c == L'U' || c == L'u' || c == L'+' || c == L' ' || c == L'-') continue;
        if (c == L'x' || c == L'X') continue;
        if (!iswxdigit(c)) return FALSE;
        normalized[j++] = (WCHAR)towupper(c);
        any = TRUE;
    }
    normalized[j] = 0;
    return any && j >= 2u && j <= 8u;
}

static BOOL ys_symbol_hex_matches(const WCHAR *text, const WCHAR *hex) {
    size_t i = 0, length = wcslen(text);
    WCHAR code[16];
    while (i < length) {
        uint32_t cp = ys_decode_codepoint(text, length, &i);
        StringCchPrintfW(code, YS_ARRAY_COUNT(code), L"%X", cp);
        if (StrStrIW(code, hex)) return TRUE;
    }
    return FALSE;
}

static BOOL ys_symbol_matches(const WCHAR *text, const WCHAR *name_zh, const WCHAR *name_en, const WCHAR *query, const WCHAR *hex, BOOL has_hex) {
    if (wcsstr(text, query)) return TRUE;
    if (name_zh && StrStrIW(name_zh, query)) return TRUE;
    if (name_en && StrStrIW(name_en, query)) return TRUE;
    if (has_hex && ys_symbol_hex_matches(text, hex)) return TRUE;
    return FALSE;
}

static BOOL ys_wide_contains(const WCHAR *text, const WCHAR *query) {
    return text && query && StrStrIW(text, query) != NULL;
}

static void ys_add_data_group(YSAppState *state, const YSGroupRecord *group, int *y) {
    size_t first_view = state->view_count;
    uint32_t i;
    if (group->flags & YS_GROUP_FLAG_SPACER) {
        *y += YS_ROW_GAP;
        return;
    }
    for (i = 0; i < group->item_count; ++i) {
        uint32_t symbol_index = g_ys_group_items[group->item_start + i];
        const YSSymbolRecord *record = &g_ys_symbols[symbol_index];
        ys_append_view(state, ys_symbol_text(symbol_index), ys_symbol_name_zh(symbol_index), ys_symbol_name_en(symbol_index), record->flags,
                       symbol_index, YS_SOURCE_DATA, 0);
    }
    if (state->view_count == first_view) return;
    if (ys_pool_string(group->title_offset)[0]) ys_layout_group_begin(state, ys_pool_string(group->title_offset), y);
    ys_layout_symbol_range(state, first_view, state->view_count - first_view, group->preferred_columns, y,
                           group->category_index, group->row_number);
}

static void ys_build_data_category(YSAppState *state, int data_index, int *y) {
    const YSCategoryRecord *category = &g_ys_categories[data_index];
    uint32_t i;
    if (category->group_count == 0) {
        const WCHAR *description = ys_pool_string(category->description_offset);
        ys_layout_group_begin(state, description[0] ? description : L"当前分类暂无可用字符。", y);
        return;
    }
    if (ys_category_symbol_count(data_index) >= YS_VIRTUAL_THRESHOLD) {
        ys_build_virtual_category(state, data_index, y);
        return;
    }
    for (i = 0; i < category->group_count; ++i) {
        const YSGroupRecord *group = &g_ys_groups[category->first_group + i];
        ys_add_data_group(state, group, y);
    }
}

static int ys_find_symbol_index(const WCHAR *text) { return ys_symbol_index_from_text(text); }
static void ys_names_for_text(const WCHAR *text, const WCHAR **name_zh, const WCHAR **name_en, uint32_t *flags, uint32_t *index) { int i=ys_find_symbol_index(text); if(i>=0){if(name_zh)*name_zh=ys_symbol_name_zh((uint32_t)i);if(name_en)*name_en=ys_symbol_name_en((uint32_t)i);if(flags)*flags=g_ys_symbols[i].flags;if(index)*index=(uint32_t)i;return;}if(name_zh)*name_zh=L"用户自定义符号";if(name_en)*name_en=L"User symbol";if(flags)*flags=ys_text_looks_emoji(text)?YS_SYMBOL_FLAG_EMOJI:0;if(index)*index=UINT32_MAX;}
static void ys_build_common(YSAppState *state, int *y) {
    size_t index;
    size_t first = state->view_count;
    for (index = 0; index < state->common.count; ++index) {
        uint32_t flags, symbol_index;
        const WCHAR *name_zh, *name_en;
        ys_names_for_text(state->common.items[index].text, &name_zh, &name_en, &flags, &symbol_index);
        ys_append_view(state, state->common.items[index].text, name_zh, name_en, flags, symbol_index,
                       YS_SOURCE_COMMON, (uint16_t)index);
    }
    ys_layout_group_begin(state, L"常用符号（拖动排序，右键删除）", y);
    ys_layout_symbol_range_fit_columns(state, first, state->view_count - first, 12, y, UINT16_MAX, 0);
}

static void ys_build_dynamic(YSAppState *state, YSDynamicList *list, uint16_t source, const WCHAR *title, int *y) {
    size_t i, first_view = state->view_count;
    for (i = 0; i < list->count; ++i) { uint32_t flags,si; const WCHAR *zh,*en; ys_names_for_text(list->items[i],&zh,&en,&flags,&si); ys_append_view(state,list->items[i],zh,en,flags,si,source,(uint16_t)i); }
    ys_layout_group_begin(state, title, y);
    ys_layout_symbol_range(state, first_view, state->view_count - first_view, 0, y, UINT16_MAX, 0);
}

static void ys_search_add_symbol(YSAppState *state, BYTE *seen, uint32_t symbol_index) {
    const YSSymbolRecord *record;
    if (symbol_index >= g_ys_symbol_count || seen[symbol_index]) return;
    seen[symbol_index] = 1;
    record = &g_ys_symbols[symbol_index];
    ys_append_view(state, ys_symbol_text(symbol_index), ys_symbol_name_zh(symbol_index), ys_symbol_name_en(symbol_index), record->flags,
                   symbol_index, YS_SOURCE_DATA, 0);
}

static BOOL ys_view_text_exists(const YSAppState *state, const WCHAR *text) {
    size_t i;
    for (i = 0; i < state->view_count; ++i) if (wcscmp(state->views[i].text, text) == 0) return TRUE;
    return FALSE;
}

static void ys_search_add_dynamic(YSAppState *state, BYTE *seen, const WCHAR *text,
                                  uint16_t source, uint16_t source_index,
                                  const WCHAR *query, const WCHAR *hex, BOOL has_hex,
                                  BOOL source_name_matches) {
    uint32_t flags, symbol_index;
    const WCHAR *name_zh, *name_en;
    if (!text || !text[0]) return;
    ys_names_for_text(text, &name_zh, &name_en, &flags, &symbol_index);
    if (symbol_index != UINT32_MAX) {
        if (seen[symbol_index]) return;
    } else if (ys_view_text_exists(state, text)) {
        return;
    }
    if (!source_name_matches && !ys_symbol_matches(text, name_zh, name_en, query, hex, has_hex)) return;
    if (symbol_index != UINT32_MAX) seen[symbol_index] = 1;
    ys_append_view(state, text, name_zh, name_en, flags, symbol_index, source, source_index);
}

static void ys_build_search(YSAppState *state, const WCHAR *query, int *y) {
    BYTE *seen;
    WCHAR hex[16];
    BOOL has_hex = ys_query_hex(query, hex, YS_ARRAY_COUNT(hex));
    size_t ci, i;
    seen = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_ys_symbol_count);
    if (!seen) return;

    /* Match every compiled symbol exactly once.  The old implementation walked
       category references and could test the same cross-category symbol several
       times.  A separate category/group pass below adds semantic matches. */
    for (i = 0; i < g_ys_symbol_count; ++i) {
        if (ys_symbol_matches(ys_symbol_text((uint32_t)i), ys_symbol_name_zh((uint32_t)i),
                              ys_symbol_name_en((uint32_t)i), query, hex, has_hex)) {
            ys_search_add_symbol(state, seen, (uint32_t)i);
        }
    }

    for (ci = 0; ci < g_ys_category_count; ++ci) {
        const YSCategoryRecord *category = &g_ys_categories[ci];
        BOOL category_matches = ys_wide_contains(ys_pool_string(category->name_offset), query);
        uint32_t gi;
        /* “全部符号” is generated from the other categories.  Ignore its group
           labels during ordinary searches to avoid traversing the same catalog
           twice; a direct search for the category name still returns all. */
        if (ci == 0 && !category_matches) continue;
        for (gi = 0; gi < category->group_count; ++gi) {
            const YSGroupRecord *group = &g_ys_groups[category->first_group + gi];
            BOOL group_matches = category_matches || ys_wide_contains(ys_pool_string(group->title_offset), query);
            uint32_t item;
            if (!group_matches) continue;
            for (item = 0; item < group->item_count; ++item) {
                ys_search_add_symbol(state, seen, g_ys_group_items[group->item_start + item]);
            }
        }
    }

    {
        BOOL recent_matches = ys_wide_contains(L"最近使用", query);
        BOOL common_matches = ys_wide_contains(L"常用符号", query);
        BOOL custom_matches = ys_wide_contains(L"自定义符号", query);
        for (i = 0; i < state->recent.count; ++i) {
            ys_search_add_dynamic(state, seen, state->recent.items[i], YS_SOURCE_RECENT, (uint16_t)i,
                                  query, hex, has_hex, recent_matches);
        }
        for (i = 0; i < state->common.count; ++i) {
            ys_search_add_dynamic(state, seen, state->common.items[i].text, YS_SOURCE_COMMON, (uint16_t)i,
                                  query, hex, has_hex, common_matches);
        }
        for (i = 0; i < state->custom.count; ++i) {
            ys_search_add_dynamic(state, seen, state->custom.items[i], YS_SOURCE_CUSTOM, (uint16_t)i,
                                  query, hex, has_hex, custom_matches);
        }
    }
    HeapFree(GetProcessHeap(), 0, seen);

    StringCchPrintfW(state->search_header, YS_ARRAY_COUNT(state->search_header), L"搜索结果：%u 个", (unsigned)state->view_count);
    ys_layout_group_begin(state, state->search_header, y);
    if (state->view_count >= YS_VIRTUAL_THRESHOLD) ys_layout_virtual_flat(state, y);
    else ys_layout_symbol_range(state, 0, state->view_count, 0, y, UINT16_MAX, 0);
}

static void ys_update_scrollbar(YSAppState *state) {
    RECT client;
    SCROLLINFO info;
    int max_position;
    GetClientRect(state->grid, &client);
    max_position = max(0, state->content_height - (client.bottom - client.top));
    state->scroll_y = max(0, min(state->scroll_y, max_position));
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = max(0, state->content_height - 1);
    info.nPage = (UINT)max(1, client.bottom - client.top);
    info.nPos = state->scroll_y;
    SetScrollInfo(state->grid, SB_VERT, &info, TRUE);
}

static void ys_rebuild(YSAppState *state, BOOL reset_scroll) {
    WCHAR query[YS_MAX_QUERY];
    ys_hide_tooltip(state);
    int y = 4;
    int data_index;
    state->view_count = 0;
    state->layout_count = 0;
    state->hover_layout = -1;
    ys_reset_virtual_layout(state);
    GetWindowTextW(state->search, query, YS_ARRAY_COUNT(query));
    if (query[0]) {
        ys_build_search(state, query, &y);
    } else if (ys_selected_is_common(state)) {
        ys_build_common(state, &y);
    } else if (ys_selected_is_custom(state)) {
        ys_build_dynamic(state, &state->custom, YS_SOURCE_CUSTOM, L"自定义符号", &y);
    } else {
        data_index = state->active_category_mapping >= 0 &&
                     (size_t)state->active_category_mapping < g_ys_category_count
                         ? state->active_category_mapping : -1;
        if (data_index >= 0) ys_build_data_category(state, data_index, &y);
    }
    state->content_height = max(y + 4, 1);
    if (reset_scroll) state->scroll_y = 0;
    ys_update_scrollbar(state);
    InvalidateRect(state->grid, NULL, TRUE);
}

static void ys_clear_hit(YSHitInfo *hit) {
    if (!hit) return;
    ZeroMemory(hit, sizeof(*hit));
    hit->layout_index = -1;
    hit->virtual_group_index = -1;
    hit->symbol_index = UINT32_MAX;
    hit->category_index = UINT16_MAX;
}

static BOOL ys_hit_test_info(YSAppState *state, int x, int y_client, YSHitInfo *hit) {
    int y;
    size_t low, high, i;
    POINT point;
    if (!state || !hit) return FALSE;
    ys_clear_hit(hit);
    y = y_client + state->scroll_y;
    point.x = x;
    point.y = y;

    if (state->virtual_data_mode) {
        for (i = 0; i < state->virtual_group_count; ++i) {
            const YSVirtualGroup *virtual_group = &state->virtual_groups[i];
            const YSGroupRecord *group = &g_ys_groups[virtual_group->group_index];
            int relative_y, row, column;
            uint32_t item_index;
            RECT rect;
            if (y < virtual_group->symbols_top || y >= virtual_group->bottom - YS_GROUP_GAP) continue;
            relative_y = y - virtual_group->symbols_top;
            row = relative_y / YS_CELL_H;
            column = (x - 4) / YS_CELL_W;
            if (x < 4 || column < 0 || column >= virtual_group->columns || row < 0 || row >= virtual_group->rows) continue;
            item_index = (uint32_t)row * virtual_group->columns + (uint32_t)column;
            if (item_index >= group->item_count) continue;
            rect.left = 4 + column * YS_CELL_W;
            rect.top = virtual_group->symbols_top + row * YS_CELL_H;
            rect.right = rect.left + YS_CELL_W - 2;
            rect.bottom = rect.top + YS_CELL_H - 2;
            if (!PtInRect(&rect, point)) continue;
            hit->valid = TRUE;
            hit->virtual_item = TRUE;
            hit->virtual_group_index = (int)i;
            hit->virtual_item_index = item_index;
            hit->symbol_index = g_ys_group_items[group->item_start + item_index];
            hit->category_index = group->category_index;
            hit->row_number = group->row_number;
            hit->column_number = (uint16_t)(item_index + 1u);
            return TRUE;
        }
    }

    if (state->virtual_flat_mode) {
        int relative_y, row, column;
        uint32_t view_index;
        RECT rect;
        if (y >= state->virtual_flat_top) {
            relative_y = y - state->virtual_flat_top;
            row = relative_y / YS_CELL_H;
            column = (x - 4) / YS_CELL_W;
            if (x >= 4 && column >= 0 && column < state->virtual_flat_columns && row >= 0) {
                view_index = (uint32_t)row * (uint32_t)state->virtual_flat_columns + (uint32_t)column;
                if (view_index < state->view_count) {
                    rect.left = 4 + column * YS_CELL_W;
                    rect.top = state->virtual_flat_top + row * YS_CELL_H;
                    rect.right = rect.left + YS_CELL_W - 2;
                    rect.bottom = rect.top + YS_CELL_H - 2;
                    if (PtInRect(&rect, point)) {
                        hit->valid = TRUE;
                        hit->virtual_item = TRUE;
                        hit->virtual_group_index = -1;
                        hit->virtual_item_index = view_index;
                        hit->view_index = view_index;
                        hit->symbol_index = state->views[view_index].symbol_index;
                        hit->category_index = UINT16_MAX;
                        hit->row_number = 0;
                        hit->column_number = (uint16_t)(view_index + 1u);
                        return TRUE;
                    }
                }
            }
        }
    }

    low = 0;
    high = state->layout_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (state->layout[middle].rect.bottom <= y) low = middle + 1u;
        else high = middle;
    }
    for (i = low; i < state->layout_count; ++i) {
        const YSLayoutItem *item = &state->layout[i];
        if (item->rect.top > y) break;
        if (item->type == YS_ITEM_SYMBOL && PtInRect(&item->rect, point)) {
            hit->valid = TRUE;
            hit->layout_index = (int)i;
            hit->view_index = item->view_index;
            hit->category_index = item->category_index;
            hit->row_number = item->row_number;
            hit->column_number = item->column_number;
            if (item->view_index < state->view_count) hit->symbol_index = state->views[item->view_index].symbol_index;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL ys_hit_is_current_hover(const YSAppState *state, const YSHitInfo *hit) {
    if (!state || !hit || !hit->valid) return state && state->hover_layout < 0 && state->hover_virtual_item < 0;
    if (hit->virtual_item) {
        return state->hover_layout < 0 && state->hover_virtual_group == hit->virtual_group_index &&
               state->hover_virtual_item == (int)hit->virtual_item_index;
    }
    return state->hover_layout == hit->layout_index && state->hover_virtual_item < 0;
}

static void ys_set_hover_hit(YSAppState *state, const YSHitInfo *hit) {
    if (!state) return;
    state->hover_layout = -1;
    state->hover_virtual_group = -1;
    state->hover_virtual_item = -1;
    if (!hit || !hit->valid) return;
    if (hit->virtual_item) {
        state->hover_virtual_group = hit->virtual_group_index;
        state->hover_virtual_item = (int)hit->virtual_item_index;
    } else {
        state->hover_layout = hit->layout_index;
    }
}

static void ys_capture_external_target(YSAppState *state) {
    HWND foreground, root, focus = NULL;
    DWORD thread_id;
    GUITHREADINFO info;
    if (!state || !state->hwnd) return;
    foreground = GetForegroundWindow();
    root = foreground ? GetAncestor(foreground, GA_ROOT) : NULL;
    if (!root || root == state->hwnd || IsChild(state->hwnd, root)) return;
    thread_id = GetWindowThreadProcessId(foreground, NULL);
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (thread_id && GetGUIThreadInfo(thread_id, &info)) {
        focus = info.hwndFocus;
        if (focus && focus != root && !IsChild(root, focus)) focus = NULL;
    }
    state->last_external_root = root;
    state->last_external_focus = focus;
    state->last_external_thread = thread_id;
}

/* Types `text` into the previously-focused external window as synthetic
 * Unicode keystrokes (KEYEVENTF_UNICODE), instead of copying to the
 * clipboard and sending Ctrl+V. This avoids depending on the target
 * accepting paste, and avoids the clipboard entirely for this path (the
 * click itself still separately copies to the clipboard via
 * ys_clipboard_set, so manual paste elsewhere keeps working). Each UTF-16
 * code unit is sent as its own key down/up pair; for characters outside
 * the BMP (most emoji, flag sequences) that means sending the high and low
 * surrogate as two consecutive events, which is exactly how Windows text
 * controls expect to reassemble a supplementary-plane character from
 * injected input. */
static BOOL ys_send_auto_insert(YSAppState *state, const WCHAR *text) {
    INPUT inputs[YS_MAX_SEQUENCE * 2u];
    HWND root, focus;
    DWORD target_thread, current_thread;
    BOOL attached = FALSE;
    size_t length, i;
    int attempt;
    UINT sent;
    if (!state || !text || !text[0]) return FALSE;
    length = wcslen(text);
    if (length > YS_ARRAY_COUNT(inputs) / 2u) return FALSE;
    root = state->last_external_root;
    focus = state->last_external_focus;
    if (!root || !IsWindow(root)) return FALSE;
    if (focus && !IsWindow(focus)) focus = NULL;
    target_thread = state->last_external_thread;
    if (!target_thread) target_thread = GetWindowThreadProcessId(root, NULL);
    current_thread = GetCurrentThreadId();
    if (target_thread && target_thread != current_thread) {
        attached = AttachThreadInput(current_thread, target_thread, TRUE);
    }
    if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
    BringWindowToTop(root);
    SetForegroundWindow(root);
    if (focus) SetFocus(focus);
    for (attempt = 0; attempt < 4; ++attempt) {
        HWND foreground = GetForegroundWindow();
        if (foreground && GetAncestor(foreground, GA_ROOT) == root) break;
        SwitchToThread();
    }
    if (focus && IsWindow(focus)) SetFocus(focus);
    ZeroMemory(inputs, sizeof(inputs[0]) * length * 2u);
    for (i = 0; i < length; ++i) {
        inputs[i * 2u].type = INPUT_KEYBOARD;
        inputs[i * 2u].ki.wScan = text[i];
        inputs[i * 2u].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[i * 2u + 1u].type = INPUT_KEYBOARD;
        inputs[i * 2u + 1u].ki.wScan = text[i];
        inputs[i * 2u + 1u].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    }
    sent = SendInput((UINT)(length * 2u), inputs, sizeof(INPUT));
    if (attached) AttachThreadInput(current_thread, target_thread, FALSE);
    return sent == (UINT)(length * 2u);
}

static void ys_flush_storage(YSAppState *state) {
    UINT flags;
    if (!state) return;
    flags = state->storage_dirty_flags;
    state->storage_dirty_flags = 0;
    KillTimer(state->hwnd, YS_TIMER_STORAGE);
    if (flags & YS_STORAGE_DIRTY_RECENT) ys_storage_save_recent(&state->recent);
    if (flags & YS_STORAGE_DIRTY_COMMON) ys_storage_save_common(&state->common);
    if (flags & YS_STORAGE_DIRTY_CUSTOM) ys_storage_save_custom(&state->custom);
    if (flags & YS_STORAGE_DIRTY_SEARCH_HISTORY) ys_storage_save_search_history(&state->search_history);
    if (flags & YS_STORAGE_DIRTY_USAGE) ys_storage_save_usage(&state->usage);
}

static void ys_schedule_storage(YSAppState *state, UINT flags) {
    if (!state || !flags) return;
    state->storage_dirty_flags |= flags;
    KillTimer(state->hwnd, YS_TIMER_STORAGE);
    SetTimer(state->hwnd, YS_TIMER_STORAGE, YS_STORAGE_FLUSH_MS, NULL);
}

static void ys_set_search_text(YSAppState *state, const WCHAR *text) {
    if (!state || !state->search) return;
    state->suppress_search_change = TRUE;
    SetWindowTextW(state->search, text ? text : L"");
    state->suppress_search_change = FALSE;
    ShowWindow(state->search_clear, text && text[0] ? SW_SHOW : SW_HIDE);
    KillTimer(state->hwnd, YS_TIMER_SEARCH);
    KillTimer(state->hwnd, YS_TIMER_SEARCH_HISTORY);
}

static void ys_search_history_commit_text(YSAppState *state, const WCHAR *text) {
    WCHAR query[YS_MAX_QUERY];
    WCHAR *start;
    WCHAR *end;
    if (!state || !text) return;
    StringCchCopyW(query, YS_ARRAY_COUNT(query), text);
    start = query;
    while (*start && iswspace(*start)) ++start;
    end = start + wcslen(start);
    while (end > start && iswspace(end[-1])) --end;
    *end = 0;
    if (!start[0]) return;
    if (ys_search_history_add_front(&state->search_history, start)) {
        ys_schedule_storage(state, YS_STORAGE_DIRTY_SEARCH_HISTORY);
    }
}

static void ys_search_history_commit_current(YSAppState *state) {
    WCHAR query[YS_MAX_QUERY];
    if (!state || !state->search) return;
    GetWindowTextW(state->search, query, YS_ARRAY_COUNT(query));
    ys_search_history_commit_text(state, query);
    state->search_history_nav = -1;
    state->search_history_draft[0] = 0;
    KillTimer(state->hwnd, YS_TIMER_SEARCH_HISTORY);
}

static void ys_search_history_navigate(YSAppState *state, int direction) {
    WCHAR current[YS_MAX_QUERY];
    if (!state || !state->search || !state->search_history.count) return;
    if (direction < 0) {
        if (state->search_history_nav < 0) {
            GetWindowTextW(state->search, state->search_history_draft,
                           YS_ARRAY_COUNT(state->search_history_draft));
            state->search_history_nav = 0;
            if (state->search_history.count > 1u &&
                _wcsicmp(state->search_history.items[0], state->search_history_draft) == 0) {
                state->search_history_nav = 1;
            }
        } else if ((size_t)(state->search_history_nav + 1) < state->search_history.count) {
            ++state->search_history_nav;
        }
        ys_set_search_text(state, state->search_history.items[state->search_history_nav]);
    } else {
        if (state->search_history_nav < 0) return;
        if (state->search_history_nav > 0) {
            if (state->search_history_nav == 1 &&
                _wcsicmp(state->search_history.items[0], state->search_history_draft) == 0) {
                state->search_history_nav = -1;
                StringCchCopyW(current, YS_ARRAY_COUNT(current), state->search_history_draft);
                state->search_history_draft[0] = 0;
                ys_set_search_text(state, current);
            } else {
                --state->search_history_nav;
                ys_set_search_text(state, state->search_history.items[state->search_history_nav]);
            }
        } else {
            state->search_history_nav = -1;
            StringCchCopyW(current, YS_ARRAY_COUNT(current), state->search_history_draft);
            state->search_history_draft[0] = 0;
            ys_set_search_text(state, current);
        }
    }
    ys_layout_controls(state);
    ys_rebuild(state, TRUE);
    SendMessageW(state->search, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
}

static LRESULT CALLBACK ys_search_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                UINT_PTR subclass_id, DWORD_PTR reference_data) {
    YSAppState *state = (YSAppState *)reference_data;
    (void)subclass_id;
    if (message == WM_GETDLGCODE) {
        return DefSubclassProc(hwnd, message, wparam, lparam) | DLGC_WANTARROWS;
    }
    if (message == WM_KEYDOWN && state) {
        if (wparam == VK_UP) {
            ys_search_history_navigate(state, -1);
            return 0;
        }
        if (wparam == VK_DOWN) {
            ys_search_history_navigate(state, 1);
            return 0;
        }
        if (wparam == VK_RETURN) {
            ys_search_history_commit_current(state);
            ys_rebuild(state, TRUE);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            ys_set_search_text(state, L"");
            ys_layout_controls(state);
            ys_rebuild(state, TRUE);
            return 0;
        }
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ys_search_subclass_proc, 1u);
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void ys_refresh_recent_strip(YSAppState *state) {
    if (state && state->recent_grid) InvalidateRect(state->recent_grid, NULL, TRUE);
}

static void ys_request_common_rebuild(YSAppState *state) {
    if (!state) return;
    if (!GetWindowTextLengthW(state->search) && ys_selected_is_common(state)) {
        state->pending_common_rebuild = TRUE;
        PostMessageW(state->hwnd, YESYMBOL_DEFERRED_REFRESH_MESSAGE, 0, 0);
    }
}

static BOOL ys_record_symbol_use(YSAppState *state, const WCHAR *text) {
    uint32_t use_count;
    if (!state || !text || !text[0]) return FALSE;
    if (ys_common_increment(&state->common, text)) {
        ys_schedule_storage(state, YS_STORAGE_DIRTY_COMMON);
        return FALSE;
    }
    use_count = ys_usage_increment(&state->usage, text);
    ys_schedule_storage(state, YS_STORAGE_DIRTY_USAGE);
    if (use_count < YS_COMMON_AUTO_ADD_THRESHOLD) return FALSE;
    if (!ys_common_add(&state->common, text, use_count)) return FALSE;
    ys_usage_remove(&state->usage, text);
    ys_schedule_storage(state, YS_STORAGE_DIRTY_COMMON | YS_STORAGE_DIRTY_USAGE);
    ys_request_common_rebuild(state);
    return TRUE;
}

static void ys_copy_view(YSAppState *state, const YSViewSymbol *source_view, const WCHAR *category, const WCHAR *prefix) {
    YSViewSymbol view;
    WCHAR copied[YS_MAX_SEQUENCE];
    BOOL recent_changed;
    if (!state || !source_view || !source_view->text || !source_view->text[0]) return;
    view = *source_view;
    StringCchCopyW(copied, YS_ARRAY_COUNT(copied), source_view->text);
    view.text = copied;
    if (!ys_clipboard_set(state->hwnd, copied)) {
        SetWindowTextW(state->status, L"复制失败：剪贴板正被其他程序占用。");
        return;
    }
    ys_set_footer(state, &view, category ? category : ys_category_name_for_view(&view), prefix ? prefix : L"已复制：");

    /* Auto insert is the latency-sensitive path.  Restore the previous target
       and type the symbol directly before sorting lists or writing the registry. */
    if (Button_GetCheck(state->auto_insert) == BST_CHECKED) ys_send_auto_insert(state, copied);

    recent_changed = ys_list_add_front_unique(&state->recent, copied);
    if (recent_changed) {
        ys_schedule_storage(state, YS_STORAGE_DIRTY_RECENT);
        ys_refresh_recent_strip(state);
    }
    if (ys_record_symbol_use(state, copied)) {
        SetWindowTextW(state->status, L"该符号已达到常用阈值，并追加到常用符号末尾。");
    }
}

static void ys_copy_hit(YSAppState *state, const YSHitInfo *hit) {
    YSViewSymbol view;
    const WCHAR *category;
    if (!ys_hit_view(state, hit, &view)) return;
    if (ys_search_is_active(state)) ys_search_history_commit_current(state);
    category = ys_hit_category_name(state, hit, &view);
    ys_copy_view(state, &view, category, L"已复制：");
}

static BOOL ys_is_combining(uint32_t cp) {
    return (cp >= 0x0300u && cp <= 0x036Fu) || (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
           (cp >= 0x1DC0u && cp <= 0x1DFFu) || (cp >= 0x20D0u && cp <= 0x20FFu) ||
           (cp >= 0xFE20u && cp <= 0xFE2Fu) || cp == 0xFE0Eu || cp == 0xFE0Fu || cp == 0x20E3u ||
           (cp >= 0x1F3FBu && cp <= 0x1F3FFu) || (cp >= 0xE0020u && cp <= 0xE007Fu);
}

static size_t ys_codepoint_units(const WCHAR *text, size_t length, size_t index) {
    if (index < length && ys_is_high_surrogate(text[index]) && index + 1u < length && ys_is_low_surrogate(text[index + 1u])) return 2u;
    return 1u;
}

static uint32_t ys_codepoint_at(const WCHAR *text, size_t length, size_t index) {
    size_t cursor = index;
    return ys_decode_codepoint(text, length, &cursor);
}

static size_t ys_next_cluster(const WCHAR *text, size_t length, size_t start) {
    size_t end, units;
    uint32_t first;
    if (start >= length) return start;
    units = ys_codepoint_units(text, length, start);
    first = ys_codepoint_at(text, length, start);
    end = start + units;
    if (first >= 0x1F1E6u && first <= 0x1F1FFu && end < length) {
        uint32_t second = ys_codepoint_at(text, length, end);
        if (second >= 0x1F1E6u && second <= 0x1F1FFu) end += ys_codepoint_units(text, length, end);
    }
    for (;;) {
        uint32_t cp;
        if (end >= length) break;
        cp = ys_codepoint_at(text, length, end);
        if (ys_is_combining(cp)) {
            end += ys_codepoint_units(text, length, end);
            continue;
        }
        if (cp == 0x200Du) {
            end += ys_codepoint_units(text, length, end);
            if (end < length) end += ys_codepoint_units(text, length, end);
            continue;
        }
        break;
    }
    return end;
}

static BOOL ys_is_skin_tone_cp(uint32_t cp) {
    return cp >= 0x1F3FBu && cp <= 0x1F3FFu;
}

static uint32_t ys_detect_skin_tone(const WCHAR *text) {
    size_t i = 0, length = wcslen(text);
    while (i < length) {
        uint32_t cp = ys_decode_codepoint(text, length, &i);
        if (ys_is_skin_tone_cp(cp)) return cp;
    }
    return 0;
}

static void ys_strip_skin_tones(const WCHAR *src, WCHAR *dst, size_t capacity) {
    size_t i = 0, length = wcslen(src), used = 0;
    if (!capacity) return;
    while (i < length && used + 3u < capacity) {
        size_t start = i;
        uint32_t cp = ys_decode_codepoint(src, length, &i);
        if (ys_is_skin_tone_cp(cp)) continue;
        while (start < i && used + 1u < capacity) dst[used++] = src[start++];
    }
    dst[used] = 0;
}

static const WCHAR *ys_tone_label(uint32_t cp) {
    switch (cp) {
    case 0x1F3FBu: return L"浅肤色";
    case 0x1F3FCu: return L"中浅肤色";
    case 0x1F3FDu: return L"中等肤色";
    case 0x1F3FEu: return L"中深肤色";
    case 0x1F3FFu: return L"深肤色";
    default: return L"默认黄色";
    }
}

static UINT ys_collect_skin_variants(const WCHAR *text, WCHAR variants[][YS_MAX_SEQUENCE], const WCHAR *labels[], size_t max_count) {
    WCHAR base[YS_MAX_SEQUENCE];
    uint32_t tones[YS_MAX_TONE_OPTIONS] = {0u, 0x1F3FBu, 0x1F3FCu, 0x1F3FDu, 0x1F3FEu, 0x1F3FFu};
    size_t ti, si;
    UINT count = 0;
    if (!text || !ys_text_looks_emoji(text)) return 0;
    ys_strip_skin_tones(text, base, YS_ARRAY_COUNT(base));
    for (ti = 0; ti < YS_ARRAY_COUNT(tones) && count < max_count; ++ti) {
        for (si = 0; si < g_ys_symbol_count; ++si) {
            WCHAR candidate_base[YS_MAX_SEQUENCE];
            const WCHAR *candidate = ys_symbol_text((uint32_t)si);
            ys_strip_skin_tones(candidate, candidate_base, YS_ARRAY_COUNT(candidate_base));
            if (wcscmp(base, candidate_base) != 0) continue;
            if (ys_detect_skin_tone(candidate) != tones[ti]) continue;
            StringCchCopyW(variants[count], YS_MAX_SEQUENCE, candidate);
            labels[count] = ys_tone_label(tones[ti]);
            ++count;
            break;
        }
    }
    return count;
}

static void ys_copy_text_direct(YSAppState *state, const WCHAR *text, const WCHAR *prefix) {
    YSViewSymbol view;
    uint32_t flags, symbol_index;
    const WCHAR *name_zh, *name_en;
    if (!state || !text || !text[0]) return;
    ys_names_for_text(text, &name_zh, &name_en, &flags, &symbol_index);
    ZeroMemory(&view, sizeof(view));
    view.text = text;
    view.name_zh = name_zh;
    view.name_en = name_en;
    view.flags = flags;
    view.symbol_index = symbol_index;
    view.source = YS_SOURCE_DATA;
    ys_copy_view(state, &view, L"Emoji 肤色变体", prefix ? prefix : L"已复制肤色变体：");
}

static void ys_add_custom_from_edit(YSAppState *state) {
    WCHAR input[512];
    size_t length, start = 0;
    BOOL changed = FALSE;
    GetWindowTextW(state->custom_text, input, YS_ARRAY_COUNT(input));
    length = wcslen(input);
    while (start < length) {
        size_t end, units;
        WCHAR cluster[YS_MAX_SEQUENCE];
        while (start < length && iswspace(input[start])) ++start;
        if (start >= length) break;
        end = ys_next_cluster(input, length, start);
        units = min(end - start, (size_t)YS_MAX_SEQUENCE - 1u);
        memcpy(cluster, input + start, units * sizeof(WCHAR));
        cluster[units] = 0;
        if (ys_list_add_back_unique(&state->custom, cluster)) changed = TRUE;
        start = end;
    }
    if (changed) ys_schedule_storage(state, YS_STORAGE_DIRTY_CUSTOM);
    SetWindowTextW(state->custom_text, L"");
    SendMessageW(state->categories, LB_SETCURSEL, state->custom_ui_index, 0);
    state->selected_ui = state->custom_ui_index;
    state->active_category_mapping = YS_CATEGORY_MAP_CUSTOM;
    ys_set_search_text(state, L"");
    ys_rebuild(state, TRUE);
}

static void ys_show_symbol_context_menu(YSAppState *state, const WCHAR *text, uint16_t source, uint16_t source_index, POINT point) {
    HMENU menu;
    HMENU skin_menu = NULL;
    UINT command;
    int common_index;
    int symbol_index;
    uint32_t previous_use_count;
    BOOL rebuild_main = FALSE;
    WCHAR symbol[YS_MAX_SEQUENCE];
    WCHAR tone_variants[YS_MAX_TONE_OPTIONS][YS_MAX_SEQUENCE];
    const WCHAR *tone_labels[YS_MAX_TONE_OPTIONS];
    UINT tone_count;
    if (!state || !text || !text[0]) return;
    StringCchCopyW(symbol, YS_ARRAY_COUNT(symbol), text);
    common_index = ys_common_find(&state->common, symbol);
    symbol_index = ys_symbol_index_from_text(symbol);
    menu = CreatePopupMenu();
    if (!menu) return;
    tone_count = ys_collect_skin_variants(symbol, tone_variants, tone_labels, YS_MAX_TONE_OPTIONS);
    if (tone_count > 1) {
        UINT i;
        skin_menu = CreatePopupMenu();
        if (skin_menu) {
            for (i = 0; i < tone_count; ++i) AppendMenuW(skin_menu, MF_STRING, ID_MENU_TONE_BASE + i, tone_labels[i]);
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)skin_menu, L"切换肤色");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        }
    }
    if (symbol_index >= 0 && ys_symbol_origin_valid((uint32_t)symbol_index)) {
        AppendMenuW(menu, MF_STRING, ID_MENU_JUMP_ORIGIN, L"跳到所在位置");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    AppendMenuW(menu, MF_STRING, common_index >= 0 ? ID_MENU_REMOVE_COMMON : ID_MENU_ADD_COMMON,
                common_index >= 0 ? L"从常用符号删除" : L"添加到常用符号");
    if (source == YS_SOURCE_RECENT) AppendMenuW(menu, MF_STRING, ID_MENU_REMOVE_RECENT, L"从最近使用删除");
    if (source == YS_SOURCE_CUSTOM) AppendMenuW(menu, MF_STRING, ID_MENU_REMOVE_CUSTOM, L"从自定义删除");
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             point.x, point.y, 0, state->hwnd, NULL);
    DestroyMenu(menu);
    if (command >= ID_MENU_TONE_BASE && command < ID_MENU_TONE_BASE + tone_count) {
        ys_copy_text_direct(state, tone_variants[command - ID_MENU_TONE_BASE], L"已复制肤色变体：");
        return;
    }
    if (command == ID_MENU_JUMP_ORIGIN && symbol_index >= 0) {
        ys_search_history_commit_current(state);
        ys_jump_to_symbol_origin(state, (uint32_t)symbol_index);
        return;
    }
    if (command == ID_MENU_ADD_COMMON) {
        previous_use_count = ys_usage_get(&state->usage, symbol);
        if (ys_common_add(&state->common, symbol, previous_use_count)) {
            ys_usage_remove(&state->usage, symbol);
            ys_schedule_storage(state, YS_STORAGE_DIRTY_COMMON | YS_STORAGE_DIRTY_USAGE);
            SetWindowTextW(state->status, L"已追加到常用符号末尾。");
            rebuild_main = ys_selected_is_common(state) || GetWindowTextLengthW(state->search) > 0;
        }
    } else if (command == ID_MENU_REMOVE_COMMON) {
        common_index = ys_common_find(&state->common, symbol);
        if (common_index >= 0 && ys_common_remove(&state->common, (size_t)common_index)) {
            /* 删除后重新从零累计，避免下一次点击立即自动加回。 */
            ys_usage_remove(&state->usage, symbol);
            ys_schedule_storage(state, YS_STORAGE_DIRTY_COMMON | YS_STORAGE_DIRTY_USAGE);
            SetWindowTextW(state->status, L"已从常用符号删除；自动加入计数已重置。");
            rebuild_main = ys_selected_is_common(state) || GetWindowTextLengthW(state->search) > 0;
        }
    } else if (command == ID_MENU_REMOVE_RECENT && source_index < state->recent.count) {
        if (ys_list_remove(&state->recent, source_index)) {
            ys_schedule_storage(state, YS_STORAGE_DIRTY_RECENT);
            ys_refresh_recent_strip(state);
            rebuild_main = GetWindowTextLengthW(state->search) > 0;
        }
    } else if (command == ID_MENU_REMOVE_CUSTOM && source_index < state->custom.count) {
        if (ys_list_remove(&state->custom, source_index)) {
            ys_schedule_storage(state, YS_STORAGE_DIRTY_CUSTOM);
            rebuild_main = ys_selected_is_custom(state) || GetWindowTextLengthW(state->search) > 0;
        }
    }
    if (rebuild_main) ys_rebuild(state, FALSE);
}

static void ys_show_context_menu(YSAppState *state, const YSHitInfo *hit, POINT point) {
    YSViewSymbol view;
    if (!ys_hit_view(state, hit, &view)) return;
    ys_show_symbol_context_menu(state, view.text, view.source, view.source_index, point);
}

static void ys_grid_scroll(YSAppState *state, int position) {
    RECT client;
    ys_hide_tooltip(state);
    int maximum;
    GetClientRect(state->grid, &client);
    maximum = max(0, state->content_height - (client.bottom - client.top));
    state->scroll_y = max(0, min(position, maximum));
    ys_update_scrollbar(state);
    InvalidateRect(state->grid, NULL, FALSE);
}

static size_t ys_recent_visible_count(const YSAppState *state) {
    if (!state) return 0;
    return min(state->recent.count, (size_t)YS_RECENT_MAX_VISIBLE);
}

static BOOL ys_recent_view(YSAppState *state, size_t index, YSViewSymbol *view) {
    uint32_t flags, symbol_index;
    const WCHAR *name_zh, *name_en;
    if (!state || !view || index >= ys_recent_visible_count(state)) return FALSE;
    ys_names_for_text(state->recent.items[index], &name_zh, &name_en, &flags, &symbol_index);
    ZeroMemory(view, sizeof(*view));
    view->text = state->recent.items[index];
    view->name_zh = name_zh;
    view->name_en = name_en;
    view->flags = flags;
    view->symbol_index = symbol_index;
    view->source = YS_SOURCE_RECENT;
    view->source_index = (uint16_t)index;
    return TRUE;
}

static RECT ys_recent_item_rect(const YSAppState *state, size_t index) {
    RECT client = {0, 0, 1, YS_RECENT_ROW_HEIGHT};
    RECT rect;
    int width;
    if (state && state->recent_grid) GetClientRect(state->recent_grid, &client);
    width = max(1, client.right - client.left);
    rect.left = (int)(((size_t)width * index) / YS_RECENT_MAX_VISIBLE) + 1;
    rect.top = 2;
    rect.right = (int)(((size_t)width * (index + 1u)) / YS_RECENT_MAX_VISIBLE) - 1;
    rect.bottom = max(rect.top + 1, client.bottom - 2);
    return rect;
}

static int ys_recent_hit_test(YSAppState *state, int x, int y) {
    size_t i, count = ys_recent_visible_count(state);
    POINT point;
    point.x = x;
    point.y = y;
    for (i = 0; i < count; ++i) {
        RECT rect = ys_recent_item_rect(state, i);
        if (PtInRect(&rect, point)) return (int)i;
    }
    return -1;
}

static void ys_hide_recent_tooltip(YSAppState *state) {
    TOOLINFOW info;
    if (!state || !state->recent_tooltip || !state->recent_grid) return;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.hwnd = state->recent_grid;
    info.uId = 1u;
    SendMessageW(state->recent_tooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&info);
}

static void ys_show_recent_tooltip(YSAppState *state, int index) {
    TOOLINFOW info;
    POINT point;
    YSViewSymbol view;
    WCHAR codes[192];
    if (!ys_recent_view(state, (size_t)index, &view) || !state->recent_tooltip) return;
    ys_format_codepoints(view.text, codes, YS_ARRAY_COUNT(codes));
    StringCchPrintfW(state->tooltip_text, YS_ARRAY_COUNT(state->tooltip_text),
                     L"符号：%s\r\n中文名称：%s\r\n英文名称：%s\r\n分类：最近使用\r\n编码：%s",
                     view.text, view.name_zh ? view.name_zh : L"", view.name_en ? view.name_en : L"", codes);
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.hwnd = state->recent_grid;
    info.uId = 1u;
    info.lpszText = state->tooltip_text;
    SendMessageW(state->recent_tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&info);
    GetCursorPos(&point);
    SendMessageW(state->recent_tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(point.x + 14, point.y + 18));
    SendMessageW(state->recent_tooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&info);
}

static BOOL ys_emoji_batch_add(YSEmojiDrawBatch *batch, const WCHAR *text,
                               const RECT *rect, COLORREF fallback_color) {
    YSEmojiDrawItem *item;
    if (!batch || !text || !text[0] || !rect || batch->count >= YS_MAX_VISIBLE_EMOJI_DRAWS) return FALSE;
    item = &batch->items[batch->count++];
    StringCchCopyW(item->text, YS_ARRAY_COUNT(item->text), text);
    item->rect = *rect;
    item->fallback_color = fallback_color;
    return TRUE;
}

static void ys_draw_emoji_batch_gdi_fallback(YSAppState *state, HDC dc, const YSEmojiDrawBatch *batch) {
    size_t index;
    if (!state || !dc || !batch) return;
    SelectObject(dc, state->emoji_font);
    SetBkMode(dc, TRANSPARENT);
    for (index = 0; index < batch->count; ++index) {
        RECT rect = batch->items[index].rect;
        SetTextColor(dc, batch->items[index].fallback_color);
        DrawTextW(dc, batch->items[index].text, -1, &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

static void ys_render_emoji_batch(YSAppState *state, HDC dc, const RECT *bounds,
                                  const YSEmojiDrawBatch *batch) {
    size_t index;
    BOOL rendered = FALSE;
    if (!state || !dc || !bounds || !batch || !batch->count) return;
    if (ys_emoji_renderer_is_available(state->emoji_renderer) &&
        ys_emoji_renderer_begin(state->emoji_renderer, dc, bounds)) {
        rendered = TRUE;
        for (index = 0; index < batch->count; ++index) {
            if (!ys_emoji_renderer_draw(state->emoji_renderer,
                                        batch->items[index].text,
                                        &batch->items[index].rect,
                                        batch->items[index].fallback_color)) {
                rendered = FALSE;
                break;
            }
        }
        if (!ys_emoji_renderer_end(state->emoji_renderer)) rendered = FALSE;
    }
    if (!rendered) ys_draw_emoji_batch_gdi_fallback(state, dc, batch);
}

static LRESULT CALLBACK ys_recent_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    YSAppState *state = (YSAppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_CREATE:
        state = (YSAppState *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        if (state) state->recent_grid = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (state) {
            int hit = ys_recent_hit_test(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (hit != state->recent_hover_index) {
                state->recent_hover_index = hit;
                InvalidateRect(hwnd, NULL, FALSE);
                if (hit >= 0) {
                    YSViewSymbol view;
                    if (ys_recent_view(state, (size_t)hit, &view)) {
                        ys_set_footer(state, &view, L"最近使用", L"");
                        ys_show_recent_tooltip(state, hit);
                    }
                } else {
                    ys_hide_recent_tooltip(state);
                }
            }
            {
                TRACKMOUSEEVENT tracking;
                ZeroMemory(&tracking, sizeof(tracking));
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = hwnd;
                TrackMouseEvent(&tracking);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            ys_hide_recent_tooltip(state);
            state->recent_hover_index = -1;
        state->category_hover_index = -1;
        state->search_history_nav = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state) {
            int hit = ys_recent_hit_test(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            YSViewSymbol view;
            if (hit >= 0 && ys_recent_view(state, (size_t)hit, &view)) ys_copy_view(state, &view, L"最近使用", L"已复制：");
        }
        return 0;
    case WM_RBUTTONUP:
        if (state) {
            int hit = ys_recent_hit_test(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (hit >= 0) {
                POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ClientToScreen(hwnd, &point);
                ys_show_symbol_context_menu(state, state->recent.items[hit], YS_SOURCE_RECENT, (uint16_t)hit, point);
            }
        }
        return 0;
    case WM_PAINT:
        if (state) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client;
            HDC memory_dc;
            HBITMAP bitmap, old_bitmap;
            YSEmojiDrawBatch emoji_batch;
            size_t i, count = ys_recent_visible_count(state);
            ZeroMemory(&emoji_batch, sizeof(emoji_batch));
            GetClientRect(hwnd, &client);
            memory_dc = CreateCompatibleDC(dc);
            bitmap = CreateCompatibleBitmap(dc, max(1, client.right), max(1, client.bottom));
            old_bitmap = (HBITMAP)SelectObject(memory_dc, bitmap);
            FillRect(memory_dc, &client, GetSysColorBrush(COLOR_WINDOW));
            SetBkMode(memory_dc, TRANSPARENT);
            if (!count) {
                RECT empty = client;
                empty.left += 8;
                SelectObject(memory_dc, state->ui_font);
                SetTextColor(memory_dc, GetSysColor(COLOR_GRAYTEXT));
                DrawTextW(memory_dc, L"暂无最近使用", -1, &empty, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            for (i = 0; i < count; ++i) {
                YSViewSymbol view;
                RECT rect = ys_recent_item_rect(state, i);
                RECT inner = rect;
                BOOL hot = ((int)i == state->recent_hover_index);
                if (!ys_recent_view(state, i, &view)) continue;
                if (hot) {
                    FillRect(memory_dc, &rect, GetSysColorBrush(COLOR_HIGHLIGHT));
                    SetTextColor(memory_dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                } else {
                    FillRect(memory_dc, &rect, GetSysColorBrush(COLOR_WINDOW));
                    SetTextColor(memory_dc, GetSysColor(COLOR_WINDOWTEXT));
                }
                DrawEdge(memory_dc, &rect, hot ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
                InflateRect(&inner, -2, -2);
                {
                    WCHAR display_buffer[YS_MAX_SEQUENCE + 4];
                    const WCHAR *display = ys_display_text(view.text, display_buffer, YS_ARRAY_COUNT(display_buffer));
                    COLORREF text_color = GetSysColor(hot ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
                    if ((view.flags & YS_SYMBOL_FLAG_EMOJI) &&
                        ys_emoji_renderer_is_available(state->emoji_renderer) &&
                        ys_emoji_batch_add(&emoji_batch, display, &inner, text_color)) {
                        /* Color Emoji is rendered in one DirectWrite/Direct2D pass below. */
                    } else {
                        SelectObject(memory_dc, (view.flags & YS_SYMBOL_FLAG_EMOJI) ? state->emoji_font : state->symbol_font);
                        DrawTextW(memory_dc, display, -1, &inner,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    }
                }
            }
            ys_render_emoji_batch(state, memory_dc, &client, &emoji_batch);
            BitBlt(dc, 0, 0, client.right, client.bottom, memory_dc, 0, 0, SRCCOPY);
            SelectObject(memory_dc, old_bitmap);
            DeleteObject(bitmap);
            DeleteDC(memory_dc);
            EndPaint(hwnd, &paint);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static HDC ys_prepare_grid_backbuffer(YSAppState *state, HDC target, int width, int height) {
    HBITMAP bitmap;
    if (!state || !target) return NULL;
    width = max(1, width);
    height = max(1, height);
    if (!state->grid_memory_dc) {
        state->grid_memory_dc = CreateCompatibleDC(target);
        if (!state->grid_memory_dc) return NULL;
    }
    if (!state->grid_bitmap || state->grid_buffer_width != width || state->grid_buffer_height != height) {
        if (state->grid_bitmap) {
            SelectObject(state->grid_memory_dc, state->grid_old_bitmap);
            DeleteObject(state->grid_bitmap);
            state->grid_bitmap = NULL;
        }
        bitmap = CreateCompatibleBitmap(target, width, height);
        if (!bitmap) return NULL;
        state->grid_old_bitmap = (HBITMAP)SelectObject(state->grid_memory_dc, bitmap);
        state->grid_bitmap = bitmap;
        state->grid_buffer_width = width;
        state->grid_buffer_height = height;
    }
    return state->grid_memory_dc;
}

static void ys_draw_grid_header(YSAppState *state, HDC dc, const WCHAR *text, RECT draw) {
    RECT line = draw;
    SelectObject(dc, state->group_font);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    FillRect(dc, &draw, GetSysColorBrush(COLOR_BTNFACE));
    line.left += 6;
    line.right -= 4;
    DrawTextW(dc, text ? text : L"", -1, &line,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void ys_draw_grid_symbol(YSAppState *state, HDC dc, const YSViewSymbol *view,
                                RECT draw, BOOL hot, YSEmojiDrawBatch *emoji_batch) {
    RECT inner = draw;
    WCHAR display_buffer[YS_MAX_SEQUENCE + 4];
    const WCHAR *display;
    COLORREF text_color;
    if (!view) return;
    if (hot) {
        FillRect(dc, &draw, GetSysColorBrush(COLOR_HIGHLIGHT));
        text_color = GetSysColor(COLOR_HIGHLIGHTTEXT);
    } else {
        FillRect(dc, &draw, GetSysColorBrush(COLOR_WINDOW));
        text_color = GetSysColor(COLOR_WINDOWTEXT);
    }
    SetTextColor(dc, text_color);
    DrawEdge(dc, &draw, hot ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    InflateRect(&inner, -2, -2);
    display = ys_display_text(view->text, display_buffer, YS_ARRAY_COUNT(display_buffer));
    if ((view->flags & YS_SYMBOL_FLAG_EMOJI) &&
        ys_emoji_renderer_is_available(state->emoji_renderer) &&
        ys_emoji_batch_add(emoji_batch, display, &inner, text_color)) {
        return;
    }
    SelectObject(dc, (view->flags & YS_SYMBOL_FLAG_EMOJI) ? state->emoji_font : state->symbol_font);
    DrawTextW(dc, display, -1, &inner,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void ys_paint_normal_layout(YSAppState *state, HDC dc, const RECT *client, YSEmojiDrawBatch *emoji_batch) {
    size_t i;
    for (i = 0; i < state->layout_count; ++i) {
        YSLayoutItem *item = &state->layout[i];
        RECT draw = item->rect;
        OffsetRect(&draw, 0, -state->scroll_y);
        if (draw.bottom < 0 || draw.top > client->bottom) continue;
        if (item->type == YS_ITEM_HEADER) {
            ys_draw_grid_header(state, dc, item->header, draw);
        } else if (item->view_index < state->view_count) {
            const YSViewSymbol *view = &state->views[item->view_index];
            BOOL hot = ((int)i == state->hover_layout && state->hover_virtual_item < 0) ||
                       (state->common_dragging && view->source == YS_SOURCE_COMMON &&
                        (int)view->source_index == state->common_drag_target);
            ys_draw_grid_symbol(state, dc, view, draw, hot, emoji_batch);
        }
    }
}

static void ys_paint_virtual_data(YSAppState *state, HDC dc, const RECT *client, YSEmojiDrawBatch *emoji_batch) {
    size_t gi;
    int visible_top = state->scroll_y;
    int visible_bottom = state->scroll_y + client->bottom;
    for (gi = 0; gi < state->virtual_group_count; ++gi) {
        const YSVirtualGroup *virtual_group = &state->virtual_groups[gi];
        const YSGroupRecord *group = &g_ys_groups[virtual_group->group_index];
        int first_row, last_row, row;
        if (virtual_group->header_top >= 0) {
            RECT header = {4, virtual_group->header_top, ys_grid_width(state) - 4, virtual_group->header_top + YS_HEADER_H};
            if (header.bottom >= visible_top && header.top <= visible_bottom) {
                OffsetRect(&header, 0, -state->scroll_y);
                ys_draw_grid_header(state, dc, ys_pool_string(group->title_offset), header);
            }
        }
        if (virtual_group->rows == 0 || virtual_group->bottom < visible_top || virtual_group->symbols_top > visible_bottom) continue;
        first_row = max(0, (visible_top - virtual_group->symbols_top) / YS_CELL_H);
        last_row = min((int)virtual_group->rows - 1, (visible_bottom - virtual_group->symbols_top) / YS_CELL_H);
        for (row = first_row; row <= last_row; ++row) {
            int column;
            for (column = 0; column < virtual_group->columns; ++column) {
                uint32_t item_index = (uint32_t)row * virtual_group->columns + (uint32_t)column;
                uint32_t symbol_index;
                YSViewSymbol view;
                RECT draw;
                BOOL hot;
                if (item_index >= group->item_count) break;
                symbol_index = g_ys_group_items[group->item_start + item_index];
                ZeroMemory(&view, sizeof(view));
                view.text = ys_symbol_text(symbol_index);
                view.name_zh = ys_symbol_name_zh(symbol_index);
                view.name_en = ys_symbol_name_en(symbol_index);
                view.flags = g_ys_symbols[symbol_index].flags;
                view.symbol_index = symbol_index;
                view.source = YS_SOURCE_DATA;
                draw.left = 4 + column * YS_CELL_W;
                draw.top = virtual_group->symbols_top + row * YS_CELL_H - state->scroll_y;
                draw.right = draw.left + YS_CELL_W - 2;
                draw.bottom = draw.top + YS_CELL_H - 2;
                hot = state->hover_layout < 0 && state->hover_virtual_group == (int)gi &&
                      state->hover_virtual_item == (int)item_index;
                ys_draw_grid_symbol(state, dc, &view, draw, hot, emoji_batch);
            }
        }
    }
}

static void ys_paint_virtual_flat(YSAppState *state, HDC dc, const RECT *client, YSEmojiDrawBatch *emoji_batch) {
    int visible_top = state->scroll_y;
    int visible_bottom = state->scroll_y + client->bottom;
    int first_row, last_row, row;
    int total_rows;
    if (!state->virtual_flat_mode || state->virtual_flat_columns <= 0 || !state->view_count) return;
    total_rows = (int)((state->view_count + (size_t)state->virtual_flat_columns - 1u) /
                       (size_t)state->virtual_flat_columns);
    first_row = max(0, (visible_top - state->virtual_flat_top) / YS_CELL_H);
    last_row = min(total_rows - 1, (visible_bottom - state->virtual_flat_top) / YS_CELL_H);
    for (row = first_row; row <= last_row; ++row) {
        int column;
        for (column = 0; column < state->virtual_flat_columns; ++column) {
            uint32_t view_index = (uint32_t)row * (uint32_t)state->virtual_flat_columns + (uint32_t)column;
            RECT draw;
            BOOL hot;
            if (view_index >= state->view_count) break;
            draw.left = 4 + column * YS_CELL_W;
            draw.top = state->virtual_flat_top + row * YS_CELL_H - state->scroll_y;
            draw.right = draw.left + YS_CELL_W - 2;
            draw.bottom = draw.top + YS_CELL_H - 2;
            hot = state->hover_layout < 0 && state->hover_virtual_group < 0 &&
                  state->hover_virtual_item == (int)view_index;
            ys_draw_grid_symbol(state, dc, &state->views[view_index], draw, hot, emoji_batch);
        }
    }
}

static int ys_common_index_from_hit(YSAppState *state, const YSHitInfo *hit) {
    YSViewSymbol view;
    if (!state || !hit || !ys_hit_view(state, hit, &view) || view.source != YS_SOURCE_COMMON) return -1;
    return view.source_index < state->common.count ? (int)view.source_index : -1;
}

static void ys_common_drag_reset(YSAppState *state) {
    if (!state) return;
    state->common_drag_source = -1;
    state->common_drag_target = -1;
    state->common_dragging = FALSE;
}

static LRESULT CALLBACK ys_grid_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    YSAppState *state = (YSAppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_CREATE:
        state = (YSAppState *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        if (state) state->grid = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (state) {
            state->grid_buffer_width = 0;
            state->grid_buffer_height = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_VSCROLL:
        if (state) {
            SCROLLINFO info;
            int position = state->scroll_y;
            ZeroMemory(&info, sizeof(info));
            info.cbSize = sizeof(info);
            info.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &info);
            switch (LOWORD(wparam)) {
            case SB_LINEUP: position -= YS_CELL_H; break;
            case SB_LINEDOWN: position += YS_CELL_H; break;
            case SB_PAGEUP: position -= 6 * YS_CELL_H; break;
            case SB_PAGEDOWN: position += 6 * YS_CELL_H; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: position = info.nTrackPos; break;
            case SB_TOP: position = 0; break;
            case SB_BOTTOM: position = state->content_height; break;
            default: return 0;
            }
            ys_grid_scroll(state, position);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state) {
            int target = state->scroll_y -
                         (GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA) * 3 * YS_CELL_H;
            MSG queued;
            /* A single physical wheel spin can post several WM_MOUSEWHEEL
             * messages before this thread gets back to the queue. Each one
             * used to trigger its own full-grid repaint, which is expensive
             * on emoji-heavy pages (every visible cell's Direct2D draw call
             * runs again per repaint). Folding any wheel messages already
             * queued for this window into one target position turns a fast
             * scroll burst into a single repaint instead of one per notch,
             * without changing the feel of a single, deliberate notch. */
            while (PeekMessageW(&queued, hwnd, WM_MOUSEWHEEL, WM_MOUSEWHEEL, PM_REMOVE)) {
                target -= (GET_WHEEL_DELTA_WPARAM(queued.wParam) / WHEEL_DELTA) * 3 * YS_CELL_H;
            }
            ys_grid_scroll(state, target);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state && ys_selected_is_common(state) && GetWindowTextLengthW(state->search) == 0) {
            YSHitInfo hit;
            int common_index;
            if (ys_hit_test_info(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &hit) &&
                (common_index = ys_common_index_from_hit(state, &hit)) >= 0) {
                state->common_drag_source = common_index;
                state->common_drag_target = common_index;
                state->common_drag_start.x = GET_X_LPARAM(lparam);
                state->common_drag_start.y = GET_Y_LPARAM(lparam);
                state->common_dragging = FALSE;
                SetCapture(hwnd);
                SetFocus(hwnd);
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (state) {
            if (state->common_drag_source >= 0 && GetCapture() == hwnd && (wparam & MK_LBUTTON)) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                if (!state->common_dragging &&
                    (abs(x - state->common_drag_start.x) >= GetSystemMetrics(SM_CXDRAG) ||
                     abs(y - state->common_drag_start.y) >= GetSystemMetrics(SM_CYDRAG))) {
                    state->common_dragging = TRUE;
                    ys_hide_tooltip(state);
                }
                if (state->common_dragging) {
                    YSHitInfo drag_hit;
                    int target = -1;
                    if (ys_hit_test_info(state, x, y, &drag_hit)) target = ys_common_index_from_hit(state, &drag_hit);
                    if (target >= 0 && target != state->common_drag_target) {
                        state->common_drag_target = target;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
                    return 0;
                }
            }
            {
                YSHitInfo hit;
                YSViewSymbol view;
                ys_hit_test_info(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &hit);
            if (!ys_hit_is_current_hover(state, &hit)) {
                ys_set_hover_hit(state, &hit);
                InvalidateRect(hwnd, NULL, FALSE);
                if (ys_hit_view(state, &hit, &view)) {
                    ys_set_footer(state, &view, ys_hit_category_name(state, &hit, &view), L"");
                    ys_show_tooltip_hit(state, &hit);
                } else {
                    ys_hide_tooltip(state);
                }
            }
            {
                TRACKMOUSEEVENT tracking;
                ZeroMemory(&tracking, sizeof(tracking));
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = hwnd;
                TrackMouseEvent(&tracking);
            }
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) {
            ys_hide_tooltip(state);
            ys_set_hover_hit(state, NULL);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state) {
            YSHitInfo hit;
            if (state->common_drag_source >= 0) {
                int source = state->common_drag_source;
                int target = state->common_drag_target;
                BOOL dragged = state->common_dragging;
                ys_common_drag_reset(state);
                if (GetCapture() == hwnd) ReleaseCapture();
                if (dragged) {
                    if (target >= 0 && ys_common_move(&state->common, (size_t)source, (size_t)target)) {
                        ys_schedule_storage(state, YS_STORAGE_DIRTY_COMMON);
                        SetWindowTextW(state->status, L"常用符号顺序已调整并保存。");
                        ys_rebuild(state, FALSE);
                    }
                    return 0;
                }
            }
            if (ys_hit_test_info(state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), &hit)) ys_copy_hit(state, &hit);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state && (HWND)lparam != hwnd) {
            ys_common_drag_reset(state);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_RBUTTONUP:
        if (state) {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            YSHitInfo hit;
            ys_hit_test_info(state, point.x, point.y, &hit);
            ClientToScreen(hwnd, &point);
            ys_show_context_menu(state, &hit, point);
        }
        return 0;
    case WM_PAINT:
        if (state) {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client;
            HDC memory_dc;
            YSEmojiDrawBatch emoji_batch;
            ZeroMemory(&emoji_batch, sizeof(emoji_batch));
            GetClientRect(hwnd, &client);
            memory_dc = ys_prepare_grid_backbuffer(state, dc, client.right, client.bottom);
            if (memory_dc) {
                FillRect(memory_dc, &client, GetSysColorBrush(COLOR_WINDOW));
                SetBkMode(memory_dc, TRANSPARENT);
                ys_paint_normal_layout(state, memory_dc, &client, &emoji_batch);
                if (state->virtual_data_mode) ys_paint_virtual_data(state, memory_dc, &client, &emoji_batch);
                if (state->virtual_flat_mode) ys_paint_virtual_flat(state, memory_dc, &client, &emoji_batch);
                ys_render_emoji_batch(state, memory_dc, &client, &emoji_batch);
                BitBlt(dc, 0, 0, client.right, client.bottom, memory_dc, 0, 0, SRCCOPY);
            } else {
                FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
            }
            EndPaint(hwnd, &paint);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void ys_create_tooltips(YSAppState *state) {
    TOOLINFOW info;
    if (!state || state->tooltip || state->recent_tooltip) return;
    state->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                     WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     state->grid, NULL, state->instance, NULL);
    if (state->tooltip) {
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
        info.hwnd = state->grid;
        info.uId = 1u;
        info.lpszText = state->tooltip_text;
        SendMessageW(state->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
        SendMessageW(state->tooltip, TTM_SETMAXTIPWIDTH, 0, 380);
        SendMessageW(state->tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);
    }
    state->recent_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON,
                                            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                            state->recent_grid, NULL, state->instance, NULL);
    if (state->recent_tooltip) {
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
        info.hwnd = state->recent_grid;
        info.uId = 1u;
        info.lpszText = state->tooltip_text;
        SendMessageW(state->recent_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
        SendMessageW(state->recent_tooltip, TTM_SETMAXTIPWIDTH, 0, 380);
        SendMessageW(state->recent_tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);
    }
}

static BOOL ys_tray_add(YSAppState *state) {
    if (!state) return FALSE;
    if (state->tray_added) return TRUE;
    ZeroMemory(&state->tray_icon, sizeof(state->tray_icon));
    state->tray_icon.cbSize = sizeof(state->tray_icon);
    state->tray_icon.hWnd = state->hwnd;
    state->tray_icon.uID = YS_TRAY_ICON_ID;
    state->tray_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    state->tray_icon.uCallbackMessage = YESYMBOL_TRAY_MESSAGE;
    state->tray_icon.hIcon = LoadIconW(state->instance, MAKEINTRESOURCEW(IDI_YESYMBOL));
    if (!state->tray_icon.hIcon) state->tray_icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    StringCchCopyW(state->tray_icon.szTip, YS_ARRAY_COUNT(state->tray_icon.szTip), L"符号大全");
    if (!Shell_NotifyIconW(NIM_ADD, &state->tray_icon)) return FALSE;
    state->tray_icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &state->tray_icon);
    state->tray_added = TRUE;
    return TRUE;
}

static void ys_tray_remove(YSAppState *state) {
    if (!state || !state->tray_added) return;
    Shell_NotifyIconW(NIM_DELETE, &state->tray_icon);
    state->tray_added = FALSE;
}

static void ys_restore_from_tray(YSAppState *state) {
    if (!state) return;
    ShowWindow(state->hwnd, SW_SHOW);
    ShowWindow(state->hwnd, SW_RESTORE);
    SetForegroundWindow(state->hwnd);
}

static void ys_show_tray_menu(YSAppState *state) {
    HMENU menu;
    POINT point;
    UINT command;
    if (!state) return;
    menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"打开符号大全");
    AppendMenuW(menu, MF_STRING, ID_TRAY_ABOUT, L"关于 YeSymbol");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    GetCursorPos(&point);
    SetForegroundWindow(state->hwnd);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                             point.x, point.y, 0, state->hwnd, NULL);
    DestroyMenu(menu);
    if (command == ID_TRAY_OPEN) {
        ys_restore_from_tray(state);
    } else if (command == ID_TRAY_ABOUT) {
        ys_show_about_dialog(state->hwnd, state->instance);
    } else if (command == ID_TRAY_EXIT) {
        HWND owner = state->hwnd;
        state->exiting = TRUE;
        DestroyWindow(owner);
        return;
    }
    PostMessageW(state->hwnd, WM_NULL, 0, 0);
}

static void ys_center_window(HWND hwnd) {
    RECT window, work;
    int width, height;
    GetWindowRect(hwnd, &window);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    width = window.right - window.left;
    height = window.bottom - window.top;
    SetWindowPos(hwnd, NULL, work.left + (work.right - work.left - width) / 2,
                 work.top + (work.bottom - work.top - height) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ys_layout_controls(YSAppState *state) {
    RECT client;
    int width, height;
    int header_y = YS_UI_MARGIN;
    int search_x, search_width, auto_x, topmost_x;
    int recent_y;
    int middle_top, middle_bottom;
    int content_left, content_width;
    int grid_top, grid_height;
    int footer_y;
    int recent_clear_x;
    BOOL custom_page;
    if (!state || !state->hwnd) return;
    GetClientRect(state->hwnd, &client);
    width = client.right;
    height = client.bottom;

    topmost_x = width - YS_UI_MARGIN - YS_TOPMOST_WIDTH;
    auto_x = topmost_x - YS_PANEL_GAP - YS_AUTO_INSERT_WIDTH;
    search_x = YS_UI_MARGIN + YS_RECENT_TOGGLE_WIDTH + YS_PANEL_GAP;
    search_width = max(180, auto_x - YS_PANEL_GAP - search_x);

    MoveWindow(state->recent_toggle, YS_UI_MARGIN, header_y,
               YS_RECENT_TOGGLE_WIDTH, YS_HEADER_ROW_HEIGHT, TRUE);
    MoveWindow(state->search, search_x, header_y,
               search_width, YS_HEADER_ROW_HEIGHT, TRUE);
    MoveWindow(state->search_clear,
               search_x + search_width - YS_SEARCH_CLEAR_WIDTH - 2, header_y + 2,
               YS_SEARCH_CLEAR_WIDTH, YS_HEADER_ROW_HEIGHT - 4, TRUE);
    MoveWindow(state->auto_insert, auto_x, header_y + 1,
               YS_AUTO_INSERT_WIDTH, YS_HEADER_ROW_HEIGHT - 2, TRUE);
    MoveWindow(state->topmost, topmost_x, header_y + 1,
               YS_TOPMOST_WIDTH, YS_HEADER_ROW_HEIGHT - 2, TRUE);

    recent_y = header_y + YS_HEADER_ROW_HEIGHT + YS_TOP_SECTION_GAP;
    recent_clear_x = width - YS_UI_MARGIN - YS_RECENT_CLEAR_WIDTH;
    if (state->recent_expanded) {
        ShowWindow(state->recent_grid, SW_SHOW);
        ShowWindow(state->recent_clear, SW_SHOW);
        MoveWindow(state->recent_grid, YS_UI_MARGIN, recent_y,
                   max(100, recent_clear_x - YS_PANEL_GAP - YS_UI_MARGIN), YS_RECENT_ROW_HEIGHT, TRUE);
        MoveWindow(state->recent_clear, recent_clear_x, recent_y + 5,
                   YS_RECENT_CLEAR_WIDTH, YS_RECENT_ROW_HEIGHT - 10, TRUE);
        middle_top = recent_y + YS_RECENT_ROW_HEIGHT + YS_TOP_SECTION_GAP;
    } else {
        ShowWindow(state->recent_grid, SW_HIDE);
        ShowWindow(state->recent_clear, SW_HIDE);
        middle_top = recent_y;
    }

    footer_y = height - YS_BOTTOM_BAR_HEIGHT;
    middle_bottom = footer_y - YS_PANEL_GAP;
    content_left = YS_UI_MARGIN + YS_CATEGORY_WIDTH + YS_PANEL_GAP;
    content_width = max(200, width - content_left - YS_UI_MARGIN);
    custom_page = ys_selected_is_custom(state) && GetWindowTextLengthW(state->search) == 0;

    MoveWindow(state->categories, YS_UI_MARGIN, middle_top,
               YS_CATEGORY_WIDTH, max(120, middle_bottom - middle_top), TRUE);

    grid_top = middle_top;
    if (custom_page) {
        ShowWindow(state->custom_label, SW_SHOW);
        ShowWindow(state->custom_text, SW_SHOW);
        ShowWindow(state->add_custom, SW_SHOW);
        MoveWindow(state->custom_label, content_left, middle_top + 2,
                   content_width, 20, TRUE);
        MoveWindow(state->custom_text, content_left, middle_top + 27,
                   max(100, content_width - YS_ADD_BUTTON_WIDTH - YS_PANEL_GAP), 26, TRUE);
        MoveWindow(state->add_custom,
                   content_left + max(100, content_width - YS_ADD_BUTTON_WIDTH - YS_PANEL_GAP) + YS_PANEL_GAP,
                   middle_top + 26, YS_ADD_BUTTON_WIDTH, 27, TRUE);
        grid_top += YS_CUSTOM_PANEL_HEIGHT;
    } else {
        ShowWindow(state->custom_label, SW_HIDE);
        ShowWindow(state->custom_text, SW_HIDE);
        ShowWindow(state->add_custom, SW_HIDE);
    }
    grid_height = max(100, middle_bottom - grid_top);
    MoveWindow(state->grid, content_left, grid_top, content_width, grid_height, TRUE);

    MoveWindow(state->footer_separator, 0, footer_y, width, 2, TRUE);
    MoveWindow(state->status, YS_UI_MARGIN, footer_y + 4,
               max(100, width - YS_UI_MARGIN * 2), YS_BOTTOM_BAR_HEIGHT - 6, TRUE);
}

static BOOL ys_add_category_mapping_item(YSAppState *state, const WCHAR *name, int mapping) {
    LRESULT result;
    if (!state || !state->category_map || !name) return FALSE;
    result = SendMessageW(state->categories, LB_ADDSTRING, 0, (LPARAM)name);
    if (result == LB_ERR || result == LB_ERRSPACE) return FALSE;
    state->category_map[state->category_item_count++] = mapping;
    return TRUE;
}

static int ys_find_data_category_by_name(const WCHAR *name) {
    size_t i;
    if (!name) return -1;
    for (i = 0; i < g_ys_category_count; ++i) {
        if (wcscmp(ys_pool_string(g_ys_categories[i].name_offset), name) == 0) return (int)i;
    }
    return -1;
}

static BOOL ys_add_named_category(YSAppState *state, const WCHAR *display_name, const WCHAR *data_name, int *selected_index, int preferred_mapping) {
    int data_index = ys_find_data_category_by_name(data_name);
    int ui_index;
    if (data_index < 0) return FALSE;
    ui_index = (int)state->category_item_count;
    if (!ys_add_category_mapping_item(state, display_name, data_index)) return FALSE;
    if (data_index == preferred_mapping) *selected_index = ui_index;
    return TRUE;
}

static void ys_add_category_items(YSAppState *state, int preferred_mapping) {
    size_t i;
    int all_data_index;
    int selected_index = 0;
    if (!state) return;
    SendMessageW(state->categories, LB_RESETCONTENT, 0, 0);
    if (state->category_map) HeapFree(GetProcessHeap(), 0, state->category_map);
    state->category_map = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           (g_ys_category_count + 8u) * sizeof(int));
    if (!state->category_map) return;
    state->category_item_count = 0;
    state->custom_ui_index = -1;
    state->all_symbols_ui_index = -1;

    ys_add_category_mapping_item(state, L"常用符号", YS_CATEGORY_MAP_COMMON);
    if (preferred_mapping == YS_CATEGORY_MAP_COMMON) selected_index = 0;

    for (i = 0; i < YS_ARRAY_COUNT(g_ys_main_category_names); ++i) {
        ys_add_named_category(state, g_ys_main_category_names[i], g_ys_main_category_names[i],
                              &selected_index, preferred_mapping);
    }

    ys_add_category_mapping_item(state, state->other_expanded ? L"其他符号⯆" : L"其他符号⯈",
                                 YS_CATEGORY_MAP_OTHER_HEADER);
    if (state->other_expanded) {
        for (i = 0; i < YS_ARRAY_COUNT(g_ys_other_category_names); ++i) {
            WCHAR display_name[64];
            StringCchPrintfW(display_name, YS_ARRAY_COUNT(display_name), L"    %s", g_ys_other_category_names[i]);
            ys_add_named_category(state, display_name, g_ys_other_category_names[i],
                                  &selected_index, preferred_mapping);
        }
    }

    all_data_index = ys_find_data_category_by_name(L"全部符号");
    if (all_data_index >= 0) {
        state->all_symbols_ui_index = (int)state->category_item_count;
        ys_add_category_mapping_item(state, L"全部符号", all_data_index);
        if (preferred_mapping == all_data_index) selected_index = state->all_symbols_ui_index;
    }
    state->custom_ui_index = (int)state->category_item_count;
    ys_add_category_mapping_item(state, L"自定义", YS_CATEGORY_MAP_CUSTOM);
    if (preferred_mapping == YS_CATEGORY_MAP_CUSTOM) selected_index = state->custom_ui_index;

    if (selected_index < 0 || (size_t)selected_index >= state->category_item_count ||
        ys_category_map_value(state, selected_index) == YS_CATEGORY_MAP_OTHER_HEADER) {
        selected_index = 0;
    }
    state->selected_ui = selected_index;
    SendMessageW(state->categories, LB_SETCURSEL, selected_index, 0);
}

static int ys_find_category_ui_index(const YSAppState *state, int mapping) {
    size_t index;
    if (!state || !state->category_map) return -1;
    for (index = 0; index < state->category_item_count; ++index) {
        if (state->category_map[index] == mapping) return (int)index;
    }
    return -1;
}

static BOOL ys_is_other_category_index(int data_index) {
    const WCHAR *name;
    size_t index;
    if (data_index < 0 || (size_t)data_index >= g_ys_category_count) return FALSE;
    name = ys_pool_string(g_ys_categories[data_index].name_offset);
    for (index = 0; index < YS_ARRAY_COUNT(g_ys_other_category_names); ++index) {
        if (wcscmp(name, g_ys_other_category_names[index]) == 0) return TRUE;
    }
    return FALSE;
}

static BOOL ys_find_symbol_location_in_category(int category_index, uint32_t symbol_index,
                                                uint32_t *group_index, uint32_t *item_index,
                                                uint16_t *row_number, uint16_t *column_number) {
    const YSCategoryRecord *category;
    uint32_t relative_group;
    if (category_index < 0 || (size_t)category_index >= g_ys_category_count ||
        symbol_index >= g_ys_symbol_count) return FALSE;
    category = &g_ys_categories[category_index];
    for (relative_group = 0; relative_group < category->group_count; ++relative_group) {
        uint32_t absolute_group = category->first_group + relative_group;
        const YSGroupRecord *group = &g_ys_groups[absolute_group];
        uint32_t item;
        if (group->flags & YS_GROUP_FLAG_SPACER) continue;
        for (item = 0; item < group->item_count; ++item) {
            if (g_ys_group_items[group->item_start + item] != symbol_index) continue;
            if (group_index) *group_index = absolute_group;
            if (item_index) *item_index = item;
            if (row_number) *row_number = group->row_number;
            if (column_number) *column_number = (uint16_t)(item + 1u);
            return TRUE;
        }
    }
    return FALSE;
}

static void ys_scroll_to_symbol_location(YSAppState *state, uint32_t symbol_index,
                                         int category_index, uint32_t group_index,
                                         uint32_t item_index, uint16_t row_number,
                                         uint16_t column_number) {
    size_t index;
    int target_y = 0;
    BOOL found = FALSE;
    state->hover_layout = -1;
    state->hover_virtual_group = -1;
    state->hover_virtual_item = -1;
    if (state->virtual_data_mode) {
        for (index = 0; index < state->virtual_group_count; ++index) {
            const YSVirtualGroup *group = &state->virtual_groups[index];
            if (group->group_index != group_index) continue;
            target_y = group->symbols_top + (int)(item_index / max(1, group->columns)) * YS_CELL_H;
            state->hover_virtual_group = (int)index;
            state->hover_virtual_item = (int)item_index;
            found = TRUE;
            break;
        }
    } else {
        for (index = 0; index < state->layout_count; ++index) {
            YSLayoutItem *item = &state->layout[index];
            if (item->type != YS_ITEM_SYMBOL || item->category_index != category_index ||
                item->row_number != row_number || item->column_number != column_number ||
                item->view_index >= state->view_count) continue;
            target_y = item->rect.top;
            state->hover_layout = (int)index;
            found = TRUE;
            break;
        }
    }
    if (found) {
        ys_grid_scroll(state, max(0, target_y - YS_CELL_H * 2));
        InvalidateRect(state->grid, NULL, FALSE);
    }
}

static void ys_jump_to_symbol_origin(YSAppState *state, uint32_t symbol_index) {
    const YSSymbolOriginRecord *origin;
    int target_category;
    uint32_t target_group;
    uint32_t target_item;
    uint16_t target_row;
    uint16_t target_column;
    int ui_index;
    const WCHAR *category_name;
    YSViewSymbol view;
    if (!state || !ys_symbol_origin_valid(symbol_index)) return;
    origin = ys_symbol_origin(symbol_index);
    target_category = origin->category_index;
    target_group = origin->group_index;
    target_item = origin->item_index;
    target_row = origin->row_number;
    target_column = origin->column_number;

    /* 补充符号不在侧边栏。对于只存在于该分类的少量字符，跳到
       “全部符号”中对应的真实位置，而悬浮信息仍显示原始分类。 */
    if (wcscmp(ys_pool_string(g_ys_categories[target_category].name_offset), L"补充符号") == 0) {
        int all_category = ys_find_data_category_by_name(L"全部符号");
        if (all_category >= 0 &&
            ys_find_symbol_location_in_category(all_category, symbol_index, &target_group, &target_item,
                                                &target_row, &target_column)) {
            target_category = all_category;
        }
    }

    if (ys_is_other_category_index(target_category)) state->other_expanded = TRUE;
    ys_add_category_items(state, target_category);
    ui_index = ys_find_category_ui_index(state, target_category);
    if (ui_index < 0) return;
    state->selected_ui = ui_index;
    state->active_category_mapping = target_category;
    SendMessageW(state->categories, LB_SETCURSEL, ui_index, 0);
    ys_set_search_text(state, L"");
    ys_layout_controls(state);
    ys_rebuild(state, TRUE);
    ys_scroll_to_symbol_location(state, symbol_index, target_category, target_group,
                                 target_item, target_row, target_column);

    ZeroMemory(&view, sizeof(view));
    view.text = ys_symbol_text(symbol_index);
    view.name_zh = ys_symbol_name_zh(symbol_index);
    view.name_en = ys_symbol_name_en(symbol_index);
    view.flags = g_ys_symbols[symbol_index].flags;
    view.symbol_index = symbol_index;
    view.source = YS_SOURCE_DATA;
    category_name = ys_pool_string(g_ys_categories[origin->category_index].name_offset);
    ys_set_footer(state, &view, category_name, L"已定位：");
    SetFocus(state->grid);
}

static void ys_invalidate_category_item(YSAppState *state, int item_index) {
    RECT rect;
    if (!state || !state->categories || item_index < 0) return;
    if (SendMessageW(state->categories, LB_GETITEMRECT, item_index, (LPARAM)&rect) != LB_ERR) {
        InvalidateRect(state->categories, &rect, FALSE);
    }
}

static LRESULT CALLBACK ys_categories_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                                    UINT_PTR subclass_id, DWORD_PTR reference_data) {
    YSAppState *state = (YSAppState *)reference_data;
    (void)subclass_id;
    if (message == WM_MOUSEMOVE && state) {
        DWORD hit = (DWORD)SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
        int item_index = HIWORD(hit) ? -1 : (int)LOWORD(hit);
        if (item_index != state->category_hover_index) {
            int old_index = state->category_hover_index;
            state->category_hover_index = item_index;
            ys_invalidate_category_item(state, old_index);
            ys_invalidate_category_item(state, item_index);
        }
        {
            TRACKMOUSEEVENT tracking;
            ZeroMemory(&tracking, sizeof(tracking));
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = hwnd;
            TrackMouseEvent(&tracking);
        }
    } else if (message == WM_MOUSELEAVE && state) {
        int old_index = state->category_hover_index;
        state->category_hover_index = -1;
        ys_invalidate_category_item(state, old_index);
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, ys_categories_subclass_proc, 1u);
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

/* How many emoji symbols to warm synchronously (blocking WM_CREATE, before
 * the window is shown) per "Emoji·" category. Large enough to cover more
 * than one screenful (YS_MAX_COLUMNS=12 wide) so a first click needs no
 * scrolling to feel warm; small enough that the one-time startup delay
 * this adds stays well under what a user would notice as a slow launch. */
#define YS_EMOJI_SYNC_PREWARM_PER_CATEGORY 60u

static BOOL ys_category_name_starts_with(uint32_t category_index, const WCHAR *prefix) {
    const WCHAR *name = ys_pool_string(g_ys_categories[category_index].name_offset);
    return wcsncmp(name, prefix, wcslen(prefix)) == 0;
}

/* Kicks off background worker threads (see ys_emoji_renderer_warm_cache_async)
 * that pre-create the DirectWrite text layout for every emoji symbol in the
 * catalog, so opening an emoji-heavy category for the first time in a
 * session is already served from cache instead of shaping ~3900 symbols on
 * demand. texts[] holds pointers into g_ys_string_pool, which is static
 * program-lifetime data, so it is safe for the background threads to read
 * them at their own pace; the warm-up call takes ownership of the texts
 * array itself and frees it when done (or immediately on failure here).
 *
 * Symbols are collected by walking categories in sidebar display order
 * (skipping index 0, "全部符号", a synthetic aggregate of every other
 * category -- see generate_bilingual_data.py) rather than by raw
 * g_ys_symbols index order, which does not track display order.
 *
 * Reordering alone still depends on the background pass having had enough
 * wall-clock time to reach a given category before the user opens it --
 * for the biggest category ("Emoji·符号与旗帜", mostly flags, and also the
 * last of the five "Emoji·" categories in display order) that was
 * observed to still lose the race in practice. So before starting the
 * background pass, this also synchronously warms a bounded first slice of
 * every "Emoji·" category (see ys_emoji_renderer_warm_cache_sync), which
 * guarantees those categories' first screenful is already cached the
 * moment the window appears, independent of background thread timing. */
static void ys_warm_emoji_layout_cache(YSAppState *state) {
    const WCHAR **texts;
    BYTE *seen;
    size_t capacity = 0, count = 0;
    uint32_t symbol_index, ci, gi, ii;
    if (!state || !ys_emoji_renderer_is_available(state->emoji_renderer)) return;
    for (symbol_index = 0; symbol_index < g_ys_symbol_count; ++symbol_index) {
        if (g_ys_symbols[symbol_index].flags & YS_SYMBOL_FLAG_EMOJI) ++capacity;
    }
    if (!capacity) return;
    texts = (const WCHAR **)HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR *) * capacity);
    if (!texts) return;
    seen = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, g_ys_symbol_count);
    if (!seen) {
        HeapFree(GetProcessHeap(), 0, texts);
        return;
    }

    for (ci = 1; ci < g_ys_category_count; ++ci) {
        const YSCategoryRecord *category;
        const WCHAR *sync_texts[YS_EMOJI_SYNC_PREWARM_PER_CATEGORY];
        size_t sync_count = 0;
        if (!ys_category_name_starts_with(ci, L"Emoji")) continue;
        category = &g_ys_categories[ci];
        for (gi = 0; gi < category->group_count && sync_count < YS_EMOJI_SYNC_PREWARM_PER_CATEGORY; ++gi) {
            const YSGroupRecord *group = &g_ys_groups[category->first_group + gi];
            for (ii = 0; ii < group->item_count && sync_count < YS_EMOJI_SYNC_PREWARM_PER_CATEGORY; ++ii) {
                symbol_index = g_ys_group_items[group->item_start + ii];
                if (symbol_index >= g_ys_symbol_count || seen[symbol_index]) continue;
                seen[symbol_index] = 1;
                if (g_ys_symbols[symbol_index].flags & YS_SYMBOL_FLAG_EMOJI) {
                    sync_texts[sync_count++] = ys_symbol_text(symbol_index);
                }
            }
        }
        if (sync_count) {
            ys_emoji_renderer_warm_cache_sync(state->emoji_renderer, sync_texts, sync_count,
                                              YS_CELL_W - 6, YS_CELL_H - 6);
        }
    }

    /* Everything else -- the rest of each "Emoji·" category beyond the
     * synchronous slice above, plus any other category's emoji symbols --
     * goes to the background pass; `seen` already excludes what was just
     * warmed synchronously. */
    for (ci = 1; ci < g_ys_category_count && count < capacity; ++ci) {
        const YSCategoryRecord *category = &g_ys_categories[ci];
        for (gi = 0; gi < category->group_count; ++gi) {
            const YSGroupRecord *group = &g_ys_groups[category->first_group + gi];
            for (ii = 0; ii < group->item_count; ++ii) {
                symbol_index = g_ys_group_items[group->item_start + ii];
                if (symbol_index >= g_ys_symbol_count || seen[symbol_index]) continue;
                seen[symbol_index] = 1;
                if (g_ys_symbols[symbol_index].flags & YS_SYMBOL_FLAG_EMOJI) {
                    texts[count++] = ys_symbol_text(symbol_index);
                }
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, seen);

    if (!count || !ys_emoji_renderer_warm_cache_async(state->emoji_renderer, texts, count,
                                                       YS_CELL_W - 6, YS_CELL_H - 6)) {
        HeapFree(GetProcessHeap(), 0, texts);
    }
}

static LRESULT CALLBACK ys_main_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    YSAppState *state = (YSAppState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (state && state->taskbar_created_message && message == state->taskbar_created_message) {
        state->tray_added = FALSE;
        ys_tray_add(state);
        return 0;
    }
    switch (message) {
    case WM_CREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        BOOL auto_insert_enabled;
        state = (YSAppState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
        if (!state) return -1;
        state->instance = create->hInstance;
        state->hwnd = hwnd;
        state->selected_ui = 0;
        state->active_category_mapping = YS_CATEGORY_MAP_COMMON;
        state->hover_layout = -1;
        state->hover_virtual_group = -1;
        state->hover_virtual_item = -1;
        state->recent_hover_index = -1;
        state->category_hover_index = -1;
        state->search_history_nav = -1;
        state->common_drag_source = -1;
        state->common_drag_target = -1;
        state->recent_expanded = TRUE;
        state->other_expanded = FALSE;
        state->virtual_category_index = -1;
        ys_capture_external_target(state);
        state->taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
        state->ui_font = ys_create_ui_font();
        state->group_font = CreateFontW(YS_GROUP_FONT_HEIGHT, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->symbol_font = CreateFontW(YS_SYMBOL_FONT_HEIGHT, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
        state->emoji_font = CreateFontW(YS_EMOJI_FONT_HEIGHT, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Emoji");
        state->emoji_renderer = ys_emoji_renderer_create((float)(-YS_EMOJI_FONT_HEIGHT));
        ys_warm_emoji_layout_cache(state);

        ys_storage_init_list(&state->recent, YS_MAX_RECENT);
        ys_common_init(&state->common);
        ys_usage_init(&state->usage);
        ys_storage_init_list(&state->custom, YS_MAX_CUSTOM);
        ys_storage_load_recent(&state->recent);
        if (!ys_storage_load_common(&state->common)) {
            size_t i;
            for (i = 0; i < g_ys_default_common_count; ++i) ys_common_add(&state->common, ys_symbol_text(g_ys_default_common_items[i]), 0);
            state->storage_dirty_flags |= YS_STORAGE_DIRTY_COMMON;
        }
        ys_storage_load_usage(&state->usage);
        ys_storage_load_custom(&state->custom);
        ys_search_history_init(&state->search_history);
        ys_storage_load_search_history(&state->search_history);
        auto_insert_enabled = ys_storage_load_bool(L"AutoInsert", FALSE);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        {
            HMENU system_menu = GetSystemMenu(hwnd, FALSE);
            if (system_menu) {
                AppendMenuW(system_menu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(system_menu, MF_STRING, ID_SYSTEM_ABOUT, L"关于 YeSymbol...");
            }
        }

        state->recent_toggle = CreateWindowW(L"BUTTON", L"最近使用 ▼",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                              0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_RECENT_TOGGLE,
                                              create->hInstance, NULL);
        state->search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_SEARCH,
                                        create->hInstance, NULL);
        SendMessageW(state->search, EM_SETCUEBANNER, TRUE,
                     (LPARAM)L"搜索符号、名称或 Unicode...");
        SendMessageW(state->search, EM_SETMARGINS, EC_RIGHTMARGIN,
                     MAKELPARAM(0, YS_SEARCH_CLEAR_WIDTH + 2));
        state->search_clear = CreateWindowW(L"BUTTON", L"×",
                                             WS_CHILD | WS_TABSTOP | BS_FLAT,
                                             0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_SEARCH_CLEAR,
                                             create->hInstance, NULL);
        state->auto_insert = CreateWindowW(L"BUTTON", L"自动插入",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_AUTO_INSERT,
                                            create->hInstance, NULL);
        Button_SetCheck(state->auto_insert, auto_insert_enabled ? BST_CHECKED : BST_UNCHECKED);
        state->topmost = CreateWindowW(L"BUTTON", L"置顶",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                       0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_TOPMOST,
                                       create->hInstance, NULL);
        Button_SetCheck(state->topmost, BST_CHECKED);

        state->recent_grid = CreateWindowExW(WS_EX_CLIENTEDGE, YESYMBOL_RECENT_CLASS, L"",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                              0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_RECENT_GRID,
                                              create->hInstance, state);
        state->recent_clear = CreateWindowW(L"BUTTON", L"🗑",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_RECENT_CLEAR,
                                             create->hInstance, NULL);

        state->categories = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                                             LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL,
                                             0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CATEGORIES,
                                             create->hInstance, NULL);
        state->grid = CreateWindowExW(WS_EX_CLIENTEDGE, YESYMBOL_GRID_CLASS, L"",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
                                      0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_GRID,
                                      create->hInstance, state);
        SetWindowSubclass(state->search, ys_search_subclass_proc, 1u, (DWORD_PTR)state);
        SetWindowSubclass(state->categories, ys_categories_subclass_proc, 1u, (DWORD_PTR)state);

        state->custom_label = CreateWindowW(L"STATIC", L"手动添加自定义符号",
                                             WS_CHILD, 0, 0, 0, 0, hwnd, NULL,
                                             create->hInstance, NULL);
        state->custom_text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                              WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                                              0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CUSTOM_TEXT,
                                              create->hInstance, NULL);
        SendMessageW(state->custom_text, EM_SETCUEBANNER, TRUE,
                     (LPARAM)L"输入一个或多个自定义符号");
        state->add_custom = CreateWindowW(L"BUTTON", L"添加",
                                          WS_CHILD | WS_TABSTOP,
                                          0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_ADD_CUSTOM,
                                          create->hInstance, NULL);
        state->footer_separator = CreateWindowW(L"STATIC", L"",
                                                 WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                                                 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_FOOTER_SEPARATOR,
                                                 create->hInstance, NULL);
        state->status = CreateWindowW(L"STATIC", L"悬停查看符号信息；单击复制。",
                                      WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE | SS_NOPREFIX,
                                      0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_STATUS,
                                      create->hInstance, NULL);

        ys_set_font(state->recent_toggle, state->ui_font);
        ys_set_font(state->search, state->ui_font);
        ys_set_font(state->search_clear, state->ui_font);
        ys_set_font(state->auto_insert, state->ui_font);
        ys_set_font(state->topmost, state->ui_font);
        ys_set_font(state->recent_clear, state->emoji_font);
        ys_set_font(state->categories, state->ui_font);
        ys_set_font(state->custom_label, state->group_font);
        ys_set_font(state->custom_text, state->ui_font);
        ys_set_font(state->add_custom, state->ui_font);
        ys_set_font(state->status, state->ui_font);

        ys_add_category_items(state, YS_CATEGORY_MAP_COMMON);
        state->active_category_mapping = YS_CATEGORY_MAP_COMMON;
        ys_layout_controls(state);
        ys_rebuild(state, TRUE);
        PostMessageW(hwnd, YESYMBOL_DEFERRED_INIT_MESSAGE, 0, 0);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lparam)->ptMinTrackSize.x = YS_WINDOW_WIDTH;
        ((MINMAXINFO *)lparam)->ptMinTrackSize.y = YS_WINDOW_HEIGHT;
        ((MINMAXINFO *)lparam)->ptMaxTrackSize.x = YS_WINDOW_WIDTH;
        ((MINMAXINFO *)lparam)->ptMaxTrackSize.y = YS_WINDOW_HEIGHT;
        return 0;
    case WM_SIZE:
        if (state) { ys_layout_controls(state); ys_rebuild(state, FALSE); }
        return 0;
    case WM_SYSCOMMAND:
        if (state && ((UINT)wparam & 0xFFF0u) == ID_SYSTEM_ABOUT) {
            ys_show_about_dialog(hwnd, state->instance);
            return 0;
        }
        break;
    case WM_MEASUREITEM:
        if (wparam == ID_CATEGORIES) {
            MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT *)lparam;
            measure->itemHeight = YS_CATEGORY_ITEM_HEIGHT;
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (wparam == ID_CATEGORIES && state) {
            DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
            WCHAR text[128];
            RECT rect = draw->rcItem;
            int mapping;
            BOOL header;
            BOOL selected;
            BOOL hovered;
            if (draw->itemID == (UINT)-1) return TRUE;
            mapping = ys_category_map_value(state, (int)draw->itemID);
            header = mapping == YS_CATEGORY_MAP_OTHER_HEADER;
            selected = (draw->itemState & ODS_SELECTED) != 0;
            hovered = state->category_hover_index == (int)draw->itemID;
            SendMessageW(state->categories, LB_GETTEXT, draw->itemID, (LPARAM)text);
            if (selected && !header) {
                FillRect(draw->hDC, &rect, GetSysColorBrush(COLOR_HIGHLIGHT));
            } else if (selected || hovered) {
                SetDCBrushColor(draw->hDC, RGB(238, 244, 250));
                FillRect(draw->hDC, &rect, (HBRUSH)GetStockObject(DC_BRUSH));
            } else {
                FillRect(draw->hDC, &rect, GetSysColorBrush(COLOR_WINDOW));
            }
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, GetSysColor(selected && !header ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            SelectObject(draw->hDC, state->ui_font);
            rect.left += 12;
            rect.right -= 6;
            DrawTextW(draw->hDC, text, -1, &rect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (!header && (draw->itemState & ODS_FOCUS)) DrawFocusRect(draw->hDC, &draw->rcItem);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (!state) break;
        if (LOWORD(wparam) == ID_SEARCH && HIWORD(wparam) == EN_CHANGE) {
            if (state->suppress_search_change) return 0;
            state->search_history_nav = -1;
            state->search_history_draft[0] = 0;
            ShowWindow(state->search_clear, GetWindowTextLengthW(state->search) ? SW_SHOW : SW_HIDE);
            ys_layout_controls(state);
            KillTimer(hwnd, YS_TIMER_SEARCH);
            KillTimer(hwnd, YS_TIMER_SEARCH_HISTORY);
            SetTimer(hwnd, YS_TIMER_SEARCH, YS_SEARCH_DEBOUNCE_MS, NULL);
            if (GetWindowTextLengthW(state->search)) {
                SetTimer(hwnd, YS_TIMER_SEARCH_HISTORY, YS_SEARCH_HISTORY_COMMIT_MS, NULL);
            }
            return 0;
        }
        if (LOWORD(wparam) == ID_SEARCH_CLEAR) {
            ys_search_history_commit_current(state);
            ys_set_search_text(state, L"");
            ys_layout_controls(state);
            ys_rebuild(state, TRUE);
            SetFocus(state->search);
            return 0;
        }
        if (LOWORD(wparam) == ID_RECENT_TOGGLE) {
            state->recent_expanded = !state->recent_expanded;
            SetWindowTextW(state->recent_toggle, state->recent_expanded ? L"最近使用 ▼" : L"最近使用 ▶");
            ys_hide_recent_tooltip(state);
            ys_layout_controls(state);
            ys_rebuild(state, FALSE);
            return 0;
        }
        if (LOWORD(wparam) == ID_RECENT_CLEAR) {
            if (state->recent.count && MessageBoxW(hwnd, L"确定清空最近使用记录吗？", L"清空最近使用",
                                                   MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK) {
                state->recent.count = 0;
                ZeroMemory(state->recent.items, sizeof(state->recent.items));
                ys_schedule_storage(state, YS_STORAGE_DIRTY_RECENT);
                ys_refresh_recent_strip(state);
                SetWindowTextW(state->status, L"最近使用记录已清空。");
                if (GetWindowTextLengthW(state->search)) ys_rebuild(state, TRUE);
            }
            return 0;
        }
        if (LOWORD(wparam) == ID_AUTO_INSERT) {
            ys_storage_save_bool(L"AutoInsert", Button_GetCheck(state->auto_insert) == BST_CHECKED);
            return 0;
        }
        if (LOWORD(wparam) == ID_CATEGORIES && HIWORD(wparam) == LBN_SELCHANGE) {
            int selection = (int)SendMessageW(state->categories, LB_GETCURSEL, 0, 0);
            int mapping = ys_category_map_value(state, selection);
            if (mapping == YS_CATEGORY_MAP_OTHER_HEADER) {
                int active_mapping = state->active_category_mapping;
                int header_index;
                state->other_expanded = !state->other_expanded;
                ys_add_category_items(state, active_mapping);
                state->active_category_mapping = active_mapping;
                header_index = ys_find_category_ui_index(state, YS_CATEGORY_MAP_OTHER_HEADER);
                if (header_index >= 0) {
                    state->selected_ui = header_index;
                    SendMessageW(state->categories, LB_SETCURSEL, header_index, 0);
                }
                ys_layout_controls(state);
                InvalidateRect(state->categories, NULL, FALSE);
                return 0;
            }
            if (selection >= 0) {
                state->selected_ui = selection;
                state->active_category_mapping = mapping;
            }
            ys_search_history_commit_current(state);
            ys_set_search_text(state, L"");
            ys_layout_controls(state);
            ys_rebuild(state, TRUE);
            return 0;
        }
        if (LOWORD(wparam) == ID_TOPMOST) {
            SetWindowPos(hwnd, Button_GetCheck(state->topmost) == BST_CHECKED ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        if (LOWORD(wparam) == ID_ADD_CUSTOM) {
            ys_add_custom_from_edit(state);
            ys_layout_controls(state);
            return 0;
        }
        break;
    case WM_TIMER:
        if (!state) return 0;
        if (wparam == YS_TIMER_FOREGROUND) {
            ys_capture_external_target(state);
        } else if (wparam == YS_TIMER_SEARCH) {
            KillTimer(hwnd, YS_TIMER_SEARCH);
            ys_rebuild(state, TRUE);
        } else if (wparam == YS_TIMER_STORAGE) {
            ys_flush_storage(state);
        } else if (wparam == YS_TIMER_SEARCH_HISTORY) {
            KillTimer(hwnd, YS_TIMER_SEARCH_HISTORY);
            ys_search_history_commit_current(state);
        }
        return 0;
    case YESYMBOL_DEFERRED_INIT_MESSAGE:
        if (state) {
            ys_create_tooltips(state);
            ys_tray_add(state);
            SetTimer(hwnd, YS_TIMER_FOREGROUND, YS_FOREGROUND_POLL_MS, NULL);
            if (state->storage_dirty_flags) {
                UINT flags = state->storage_dirty_flags;
                state->storage_dirty_flags = 0;
                ys_schedule_storage(state, flags);
            }
        }
        return 0;
    case YESYMBOL_DEFERRED_REFRESH_MESSAGE:
        if (state && state->pending_common_rebuild) {
            state->pending_common_rebuild = FALSE;
            if (!GetWindowTextLengthW(state->search) && ys_selected_is_common(state)) ys_rebuild(state, FALSE);
        }
        return 0;
    case YESYMBOL_ACTIVATE_MESSAGE:
        if (state) ys_restore_from_tray(state);
        return 0;
    case YESYMBOL_TRAY_MESSAGE:
        if (state) {
            UINT event_code = LOWORD(lparam);
            if (event_code == WM_CONTEXTMENU || event_code == WM_RBUTTONUP) {
                ys_show_tray_menu(state);
            } else if (event_code == WM_LBUTTONDBLCLK || event_code == NIN_SELECT || event_code == NIN_KEYSELECT) {
                ys_restore_from_tray(state);
            }
        }
        return 0;
    case WM_CLOSE:
        if (state && !state->exiting) {
            ys_hide_tooltip(state);
            ys_hide_recent_tooltip(state);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) {
            KillTimer(hwnd, YS_TIMER_FOREGROUND);
            KillTimer(hwnd, YS_TIMER_SEARCH);
            KillTimer(hwnd, YS_TIMER_STORAGE);
            KillTimer(hwnd, YS_TIMER_SEARCH_HISTORY);
            ys_flush_storage(state);
            if (state->search) RemoveWindowSubclass(state->search, ys_search_subclass_proc, 1u);
            if (state->categories) RemoveWindowSubclass(state->categories, ys_categories_subclass_proc, 1u);
            ys_tray_remove(state);
            ys_hide_tooltip(state);
            ys_hide_recent_tooltip(state);
            if (state->tooltip) DestroyWindow(state->tooltip);
            if (state->recent_tooltip) DestroyWindow(state->recent_tooltip);
            if (state->views) HeapFree(GetProcessHeap(), 0, state->views);
            if (state->layout) HeapFree(GetProcessHeap(), 0, state->layout);
            if (state->virtual_groups) HeapFree(GetProcessHeap(), 0, state->virtual_groups);
            if (state->category_map) HeapFree(GetProcessHeap(), 0, state->category_map);
            if (state->grid_memory_dc) {
                if (state->grid_old_bitmap) SelectObject(state->grid_memory_dc, state->grid_old_bitmap);
                if (state->grid_bitmap) DeleteObject(state->grid_bitmap);
                DeleteDC(state->grid_memory_dc);
            }
            ys_emoji_renderer_destroy(state->emoji_renderer);
            if (state->group_font) DeleteObject(state->group_font);
            if (state->symbol_font) DeleteObject(state->symbol_font);
            if (state->emoji_font) DeleteObject(state->emoji_font);
            if (state->ui_font && state->ui_font != GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(state->ui_font);
            HeapFree(GetProcessHeap(), 0, state);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int ys_run_ui(HINSTANCE instance, int show_command) {
    WNDCLASSEXW main_class, grid_class, recent_class;
    HWND hwnd;
    MSG message;
    ZeroMemory(&grid_class, sizeof(grid_class));
    grid_class.cbSize = sizeof(grid_class);
    grid_class.hInstance = instance;
    grid_class.lpfnWndProc = ys_grid_proc;
    grid_class.lpszClassName = YESYMBOL_GRID_CLASS;
    grid_class.hCursor = LoadCursorW(NULL, IDC_HAND);
    grid_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    if (!RegisterClassExW(&grid_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 3;

    ZeroMemory(&recent_class, sizeof(recent_class));
    recent_class.cbSize = sizeof(recent_class);
    recent_class.hInstance = instance;
    recent_class.lpfnWndProc = ys_recent_proc;
    recent_class.lpszClassName = YESYMBOL_RECENT_CLASS;
    recent_class.hCursor = LoadCursorW(NULL, IDC_HAND);
    recent_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    if (!RegisterClassExW(&recent_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 4;

    ZeroMemory(&main_class, sizeof(main_class));
    main_class.cbSize = sizeof(main_class);
    main_class.hInstance = instance;
    main_class.lpfnWndProc = ys_main_proc;
    main_class.lpszClassName = YESYMBOL_WINDOW_CLASS;
    main_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    main_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_YESYMBOL));
    main_class.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_YESYMBOL), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!main_class.hIcon) main_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (!main_class.hIconSm) main_class.hIconSm = main_class.hIcon;
    main_class.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    if (!RegisterClassExW(&main_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 5;

    hwnd = CreateWindowExW(WS_EX_TOPMOST, YESYMBOL_WINDOW_CLASS, L"符号大全 - 添加符号",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, YS_WINDOW_WIDTH, YS_WINDOW_HEIGHT,
                           NULL, NULL, instance, NULL);
    if (!hwnd) return 6;
    ys_center_window(hwnd);
    ShowWindow(hwnd, show_command == SW_HIDE ? SW_SHOW : show_command);
    UpdateWindow(hwnd);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return (int)message.wParam;
}
