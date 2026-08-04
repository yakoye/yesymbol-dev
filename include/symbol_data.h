#pragma once
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#define YS_SYMBOL_FLAG_EMOJI 0x0001u
#define YS_GROUP_FLAG_SPACER 0x0001u
typedef struct YSSymbolRecord { uint32_t text_offset; uint32_t name_zh_offset; uint32_t name_en_offset; uint16_t text_length; uint16_t flags; } YSSymbolRecord;
typedef struct YSGroupRecord { uint16_t category_index; uint16_t preferred_columns; uint16_t row_number; uint16_t flags; uint32_t title_offset; uint32_t item_start; uint32_t item_count; } YSGroupRecord;
typedef struct YSCategoryRecord { uint32_t name_offset; uint32_t description_offset; uint32_t first_group; uint16_t group_count; uint16_t reserved; } YSCategoryRecord;
extern const WCHAR g_ys_string_pool[];
extern const YSSymbolRecord g_ys_symbols[];
extern const YSGroupRecord g_ys_groups[];
extern const YSCategoryRecord g_ys_categories[];
extern const uint32_t g_ys_group_items[];
extern const uint32_t g_ys_default_common_items[];
extern const size_t g_ys_symbol_count, g_ys_group_count, g_ys_category_count, g_ys_group_item_count, g_ys_default_common_count;
static __inline const WCHAR *ys_pool_string(uint32_t offset) { return g_ys_string_pool + offset; }
static __inline const WCHAR *ys_symbol_text(uint32_t index) { return ys_pool_string(g_ys_symbols[index].text_offset); }
static __inline const WCHAR *ys_symbol_name_zh(uint32_t index) { return ys_pool_string(g_ys_symbols[index].name_zh_offset); }
static __inline const WCHAR *ys_symbol_name_en(uint32_t index) { return ys_pool_string(g_ys_symbols[index].name_en_offset); }

int ys_symbol_index_from_text(const WCHAR *text);
