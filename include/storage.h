#pragma once
#include "yesymbol.h"
#define YS_MAX_COMMON 256

typedef struct YSDynamicList {
    WCHAR items[YS_MAX_CUSTOM][YS_MAX_SEQUENCE];
    size_t count;
    size_t capacity;
} YSDynamicList;

typedef struct YSCommonItem {
    WCHAR text[YS_MAX_SEQUENCE];
    uint32_t use_count;
    uint32_t serial;
} YSCommonItem;

typedef struct YSCommonList {
    YSCommonItem items[YS_MAX_COMMON];
    size_t count;
    uint32_t next_serial;
} YSCommonList;

typedef struct YSSearchHistory {
    WCHAR items[YS_MAX_SEARCH_HISTORY][YS_MAX_QUERY];
    size_t count;
} YSSearchHistory;

void ys_storage_init_list(YSDynamicList *, size_t);
void ys_storage_load_recent(YSDynamicList *);
void ys_storage_load_custom(YSDynamicList *);
void ys_storage_save_recent(const YSDynamicList *);
void ys_storage_save_custom(const YSDynamicList *);
BOOL ys_list_add_front_unique(YSDynamicList *, const WCHAR *);
BOOL ys_list_add_back_unique(YSDynamicList *, const WCHAR *);
BOOL ys_list_remove(YSDynamicList *, size_t);

void ys_common_init(YSCommonList *);
BOOL ys_storage_load_common(YSCommonList *);
void ys_storage_save_common(const YSCommonList *);
int ys_common_find(const YSCommonList *, const WCHAR *);
BOOL ys_common_add(YSCommonList *, const WCHAR *, uint32_t);
BOOL ys_common_increment(YSCommonList *, const WCHAR *);
BOOL ys_common_remove(YSCommonList *, size_t);
void ys_common_sort(YSCommonList *);

BOOL ys_storage_load_bool(const WCHAR *value_name, BOOL default_value);
void ys_storage_save_bool(const WCHAR *value_name, BOOL value);

void ys_search_history_init(YSSearchHistory *history);
void ys_storage_load_search_history(YSSearchHistory *history);
void ys_storage_save_search_history(const YSSearchHistory *history);
BOOL ys_search_history_add_front(YSSearchHistory *history, const WCHAR *query);
