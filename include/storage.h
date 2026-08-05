#pragma once
#include "yesymbol.h"

#define YS_MAX_COMMON 256
#define YS_MAX_COMMON_SUPPRESSED 256
#define YS_MAX_USAGE 2048

#define YS_COMMON_ORIGIN_DEFAULT 1u
#define YS_COMMON_ORIGIN_USER 2u

typedef struct YSDynamicList {
    WCHAR items[YS_MAX_CUSTOM][YS_MAX_SEQUENCE];
    size_t count;
    size_t capacity;
} YSDynamicList;

typedef struct YSCommonItem {
    WCHAR text[YS_MAX_SEQUENCE];
    uint32_t use_count;
    uint32_t serial;
    uint32_t origin;
} YSCommonItem;

typedef struct YSCommonList {
    YSCommonItem items[YS_MAX_COMMON];
    size_t count;
    uint32_t next_serial;
    WCHAR suppressed[YS_MAX_COMMON_SUPPRESSED][YS_MAX_SEQUENCE];
    size_t suppressed_count;
    uint32_t loaded_version;
} YSCommonList;

typedef struct YSUsageItem {
    WCHAR text[YS_MAX_SEQUENCE];
    uint32_t use_count;
    uint32_t serial;
} YSUsageItem;

typedef struct YSUsageList {
    YSUsageItem items[YS_MAX_USAGE];
    size_t count;
    uint32_t next_serial;
} YSUsageList;

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
BOOL ys_common_add_user(YSCommonList *, const WCHAR *, uint32_t);
BOOL ys_common_increment(YSCommonList *, const WCHAR *);
BOOL ys_common_remove(YSCommonList *, size_t);
BOOL ys_common_move(YSCommonList *, size_t, size_t);
BOOL ys_common_reconcile(YSCommonList *, const WCHAR *const *, size_t);
BOOL ys_common_is_suppressed(const YSCommonList *, const WCHAR *);

void ys_usage_init(YSUsageList *);
BOOL ys_storage_load_usage(YSUsageList *);
void ys_storage_save_usage(const YSUsageList *);
uint32_t ys_usage_increment(YSUsageList *, const WCHAR *);
uint32_t ys_usage_get(const YSUsageList *, const WCHAR *);
BOOL ys_usage_remove(YSUsageList *, const WCHAR *);

BOOL ys_storage_load_bool(const WCHAR *value_name, BOOL default_value);
void ys_storage_save_bool(const WCHAR *value_name, BOOL value);

void ys_search_history_init(YSSearchHistory *history);
void ys_storage_load_search_history(YSSearchHistory *history);
void ys_storage_save_search_history(const YSSearchHistory *history);
BOOL ys_search_history_add_front(YSSearchHistory *history, const WCHAR *query);
