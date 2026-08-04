#include "storage.h"
#include <strsafe.h>
#include <wchar.h>
#include <string.h>

#define YS_COMMON_MAGIC 0x31435359u
#define YS_COMMON_VERSION 2u
#define YS_USAGE_MAGIC 0x31555359u
#define YS_USAGE_VERSION 1u

static const WCHAR *YS_REG_PATH = L"Software\\YeTools\\YeSymbol";

typedef struct YSCommonPersist {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t next_serial;
    YSCommonItem items[YS_MAX_COMMON];
} YSCommonPersist;

typedef struct YSUsagePersist {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t next_serial;
    YSUsageItem items[YS_MAX_USAGE];
} YSUsagePersist;

static BOOL ys_text_equal(const WCHAR *left, const WCHAR *right) {
    return left && right && wcscmp(left, right) == 0;
}

static void ys_copy_sequence(WCHAR *destination, const WCHAR *source) {
    StringCchCopyW(destination, YS_MAX_SEQUENCE, source ? source : L"");
}

void ys_storage_init_list(YSDynamicList *list, size_t capacity) {
    if (!list) return;
    ZeroMemory(list, sizeof(*list));
    list->capacity = capacity > YS_MAX_CUSTOM ? YS_MAX_CUSTOM : capacity;
}

BOOL ys_list_add_front_unique(YSDynamicList *list, const WCHAR *text) {
    size_t index;
    size_t found = (size_t)-1;
    size_t last;
    if (!list || !text || !text[0] || !list->capacity) return FALSE;
    for (index = 0; index < list->count; ++index) {
        if (ys_text_equal(list->items[index], text)) {
            found = index;
            break;
        }
    }
    if (found == 0) return FALSE;
    if (found != (size_t)-1) {
        for (index = found; index > 0; --index) ys_copy_sequence(list->items[index], list->items[index - 1]);
    } else {
        last = list->count < list->capacity ? list->count : list->capacity - 1u;
        for (index = last; index > 0; --index) ys_copy_sequence(list->items[index], list->items[index - 1]);
        if (list->count < list->capacity) ++list->count;
    }
    ys_copy_sequence(list->items[0], text);
    return TRUE;
}

BOOL ys_list_add_back_unique(YSDynamicList *list, const WCHAR *text) {
    size_t index;
    if (!list || !text || !text[0] || list->count >= list->capacity) return FALSE;
    for (index = 0; index < list->count; ++index) {
        if (ys_text_equal(list->items[index], text)) return FALSE;
    }
    ys_copy_sequence(list->items[list->count++], text);
    return TRUE;
}

BOOL ys_list_remove(YSDynamicList *list, size_t remove_index) {
    size_t index;
    if (!list || remove_index >= list->count) return FALSE;
    for (index = remove_index; index + 1u < list->count; ++index) {
        ys_copy_sequence(list->items[index], list->items[index + 1u]);
    }
    --list->count;
    list->items[list->count][0] = 0;
    return TRUE;
}

static void ys_load_multi_string(const WCHAR *value_name, YSDynamicList *list) {
    HKEY key;
    DWORD type = 0;
    DWORD bytes = 0;
    WCHAR *buffer;
    WCHAR *cursor;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, value_name, NULL, &type, NULL, &bytes) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || bytes < sizeof(WCHAR) * 2u) {
        RegCloseKey(key);
        return;
    }
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR) * 2u);
    if (buffer && RegQueryValueExW(key, value_name, NULL, &type, (BYTE *)buffer, &bytes) == ERROR_SUCCESS) {
        for (cursor = buffer; *cursor && list->count < list->capacity; cursor += wcslen(cursor) + 1u) {
            ys_list_add_back_unique(list, cursor);
        }
    }
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    RegCloseKey(key);
}

static void ys_save_multi_string(const WCHAR *value_name, const YSDynamicList *list) {
    HKEY key;
    DWORD disposition;
    size_t index;
    size_t total = 2u;
    size_t offset = 0;
    WCHAR *buffer;
    if (!list) return;
    for (index = 0; index < list->count; ++index) total += wcslen(list->items[index]) + 1u;
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total * sizeof(WCHAR));
    if (!buffer) return;
    for (index = 0; index < list->count; ++index) {
        size_t length = wcslen(list->items[index]);
        memcpy(buffer + offset, list->items[index], (length + 1u) * sizeof(WCHAR));
        offset += length + 1u;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL,
                        &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, value_name, 0, REG_MULTI_SZ, (const BYTE *)buffer,
                       (DWORD)(total * sizeof(WCHAR)));
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}

void ys_storage_load_recent(YSDynamicList *list) { ys_load_multi_string(L"Recent", list); }
void ys_storage_load_custom(YSDynamicList *list) { ys_load_multi_string(L"Custom", list); }
void ys_storage_save_recent(const YSDynamicList *list) { ys_save_multi_string(L"Recent", list); }
void ys_storage_save_custom(const YSDynamicList *list) { ys_save_multi_string(L"Custom", list); }

void ys_common_init(YSCommonList *list) {
    if (!list) return;
    ZeroMemory(list, sizeof(*list));
    list->next_serial = 1u;
}

int ys_common_find(const YSCommonList *list, const WCHAR *text) {
    size_t index;
    if (!list || !text) return -1;
    for (index = 0; index < list->count; ++index) {
        if (wcscmp(list->items[index].text, text) == 0) return (int)index;
    }
    return -1;
}

BOOL ys_common_add(YSCommonList *list, const WCHAR *text, uint32_t use_count) {
    YSCommonItem *item;
    if (!list || !text || !text[0] || list->count >= YS_MAX_COMMON || ys_common_find(list, text) >= 0) return FALSE;
    item = &list->items[list->count++];
    ZeroMemory(item, sizeof(*item));
    ys_copy_sequence(item->text, text);
    item->use_count = use_count;
    item->serial = list->next_serial++;
    if (!list->next_serial) list->next_serial = 1u;
    return TRUE;
}

BOOL ys_common_increment(YSCommonList *list, const WCHAR *text) {
    int index = ys_common_find(list, text);
    if (index < 0) return FALSE;
    if (list->items[index].use_count != UINT32_MAX) ++list->items[index].use_count;
    return TRUE;
}

BOOL ys_common_remove(YSCommonList *list, size_t remove_index) {
    size_t index;
    if (!list || remove_index >= list->count) return FALSE;
    for (index = remove_index; index + 1u < list->count; ++index) list->items[index] = list->items[index + 1u];
    --list->count;
    ZeroMemory(&list->items[list->count], sizeof(list->items[0]));
    return TRUE;
}

BOOL ys_common_move(YSCommonList *list, size_t from_index, size_t to_index) {
    YSCommonItem moving;
    size_t index;
    if (!list || from_index >= list->count || to_index >= list->count || from_index == to_index) return FALSE;
    moving = list->items[from_index];
    if (from_index < to_index) {
        for (index = from_index; index < to_index; ++index) list->items[index] = list->items[index + 1u];
    } else {
        for (index = from_index; index > to_index; --index) list->items[index] = list->items[index - 1u];
    }
    list->items[to_index] = moving;
    return TRUE;
}

static void ys_common_normalize(YSCommonList *list) {
    size_t read_index;
    size_t write_index = 0;
    uint32_t maximum_serial = 0;
    if (!list) return;
    for (read_index = 0; read_index < list->count && read_index < YS_MAX_COMMON; ++read_index) {
        size_t existing;
        YSCommonItem item = list->items[read_index];
        if (!item.text[0]) continue;
        for (existing = 0; existing < write_index; ++existing) {
            if (wcscmp(list->items[existing].text, item.text) == 0) break;
        }
        if (existing < write_index) continue;
        if (!item.serial) item.serial = maximum_serial + 1u;
        if (item.serial > maximum_serial) maximum_serial = item.serial;
        list->items[write_index++] = item;
    }
    list->count = write_index;
    while (write_index < YS_MAX_COMMON) ZeroMemory(&list->items[write_index++], sizeof(list->items[0]));
    if (list->next_serial <= maximum_serial) list->next_serial = maximum_serial + 1u;
    if (!list->next_serial) list->next_serial = 1u;
}

static BOOL ys_load_common_value(HKEY key, const WCHAR *value_name, uint32_t expected_version,
                                 YSCommonList *list) {
    DWORD type = 0;
    DWORD bytes = sizeof(YSCommonPersist);
    YSCommonPersist *persist;
    BOOL loaded = FALSE;
    persist = (YSCommonPersist *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*persist));
    if (!persist) return FALSE;
    if (RegQueryValueExW(key, value_name, NULL, &type, (BYTE *)persist, &bytes) == ERROR_SUCCESS &&
        type == REG_BINARY && bytes == sizeof(*persist) && persist->magic == YS_COMMON_MAGIC &&
        persist->version == expected_version && persist->count <= YS_MAX_COMMON) {
        list->count = persist->count;
        list->next_serial = persist->next_serial ? persist->next_serial : 1u;
        memcpy(list->items, persist->items, persist->count * sizeof(YSCommonItem));
        ys_common_normalize(list);
        loaded = TRUE;
    }
    HeapFree(GetProcessHeap(), 0, persist);
    return loaded;
}

BOOL ys_storage_load_common(YSCommonList *list) {
    HKEY key;
    BOOL loaded = FALSE;
    BOOL migrated = FALSE;
    if (!list) return FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return FALSE;
    loaded = ys_load_common_value(key, L"CommonV2", YS_COMMON_VERSION, list);
    if (!loaded) {
        loaded = ys_load_common_value(key, L"CommonV1", 1u, list);
        migrated = loaded;
    }
    RegCloseKey(key);
    if (migrated) ys_storage_save_common(list);
    return loaded;
}

void ys_storage_save_common(const YSCommonList *list) {
    HKEY key;
    DWORD disposition;
    YSCommonPersist *persist;
    if (!list) return;
    persist = (YSCommonPersist *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*persist));
    if (!persist) return;
    persist->magic = YS_COMMON_MAGIC;
    persist->version = YS_COMMON_VERSION;
    persist->count = (uint32_t)list->count;
    persist->next_serial = list->next_serial;
    memcpy(persist->items, list->items, list->count * sizeof(YSCommonItem));
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL,
                        &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"CommonV2", 0, REG_BINARY, (const BYTE *)persist, sizeof(*persist));
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, persist);
}

void ys_usage_init(YSUsageList *list) {
    if (!list) return;
    ZeroMemory(list, sizeof(*list));
    list->next_serial = 1u;
}

static int ys_usage_find(const YSUsageList *list, const WCHAR *text) {
    size_t index;
    if (!list || !text) return -1;
    for (index = 0; index < list->count; ++index) {
        if (wcscmp(list->items[index].text, text) == 0) return (int)index;
    }
    return -1;
}

uint32_t ys_usage_get(const YSUsageList *list, const WCHAR *text) {
    int index = ys_usage_find(list, text);
    return index >= 0 ? list->items[index].use_count : 0u;
}

BOOL ys_usage_remove(YSUsageList *list, const WCHAR *text) {
    int found = ys_usage_find(list, text);
    size_t index;
    if (found < 0) return FALSE;
    for (index = (size_t)found; index + 1u < list->count; ++index) list->items[index] = list->items[index + 1u];
    --list->count;
    ZeroMemory(&list->items[list->count], sizeof(list->items[0]));
    return TRUE;
}

uint32_t ys_usage_increment(YSUsageList *list, const WCHAR *text) {
    int found;
    YSUsageItem *item;
    size_t replace_index = 0;
    size_t index;
    if (!list || !text || !text[0]) return 0u;
    found = ys_usage_find(list, text);
    if (found >= 0) {
        item = &list->items[found];
        if (item->use_count != UINT32_MAX) ++item->use_count;
        item->serial = list->next_serial++;
        if (!list->next_serial) list->next_serial = 1u;
        return item->use_count;
    }
    if (list->count < YS_MAX_USAGE) {
        item = &list->items[list->count++];
    } else {
        for (index = 1; index < list->count; ++index) {
            const YSUsageItem *candidate = &list->items[index];
            const YSUsageItem *replace = &list->items[replace_index];
            if (candidate->use_count < replace->use_count ||
                (candidate->use_count == replace->use_count && candidate->serial < replace->serial)) {
                replace_index = index;
            }
        }
        item = &list->items[replace_index];
    }
    ZeroMemory(item, sizeof(*item));
    ys_copy_sequence(item->text, text);
    item->use_count = 1u;
    item->serial = list->next_serial++;
    if (!list->next_serial) list->next_serial = 1u;
    return item->use_count;
}

static void ys_usage_normalize(YSUsageList *list) {
    size_t read_index;
    size_t write_index = 0;
    uint32_t maximum_serial = 0;
    if (!list) return;
    for (read_index = 0; read_index < list->count && read_index < YS_MAX_USAGE; ++read_index) {
        size_t existing;
        YSUsageItem item = list->items[read_index];
        if (!item.text[0] || !item.use_count) continue;
        for (existing = 0; existing < write_index; ++existing) {
            if (wcscmp(list->items[existing].text, item.text) == 0) break;
        }
        if (existing < write_index) {
            if (item.use_count > list->items[existing].use_count) list->items[existing].use_count = item.use_count;
            continue;
        }
        if (!item.serial) item.serial = maximum_serial + 1u;
        if (item.serial > maximum_serial) maximum_serial = item.serial;
        list->items[write_index++] = item;
    }
    list->count = write_index;
    while (write_index < YS_MAX_USAGE) ZeroMemory(&list->items[write_index++], sizeof(list->items[0]));
    if (list->next_serial <= maximum_serial) list->next_serial = maximum_serial + 1u;
    if (!list->next_serial) list->next_serial = 1u;
}

BOOL ys_storage_load_usage(YSUsageList *list) {
    HKEY key;
    DWORD type = 0;
    DWORD bytes = sizeof(YSUsagePersist);
    YSUsagePersist *persist;
    BOOL loaded = FALSE;
    if (!list || RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return FALSE;
    persist = (YSUsagePersist *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*persist));
    if (persist && RegQueryValueExW(key, L"UsageV1", NULL, &type, (BYTE *)persist, &bytes) == ERROR_SUCCESS &&
        type == REG_BINARY && bytes == sizeof(*persist) && persist->magic == YS_USAGE_MAGIC &&
        persist->version == YS_USAGE_VERSION && persist->count <= YS_MAX_USAGE) {
        list->count = persist->count;
        list->next_serial = persist->next_serial ? persist->next_serial : 1u;
        memcpy(list->items, persist->items, persist->count * sizeof(YSUsageItem));
        ys_usage_normalize(list);
        loaded = TRUE;
    }
    if (persist) HeapFree(GetProcessHeap(), 0, persist);
    RegCloseKey(key);
    return loaded;
}

void ys_storage_save_usage(const YSUsageList *list) {
    HKEY key;
    DWORD disposition;
    YSUsagePersist *persist;
    if (!list) return;
    persist = (YSUsagePersist *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*persist));
    if (!persist) return;
    persist->magic = YS_USAGE_MAGIC;
    persist->version = YS_USAGE_VERSION;
    persist->count = (uint32_t)list->count;
    persist->next_serial = list->next_serial;
    memcpy(persist->items, list->items, list->count * sizeof(YSUsageItem));
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL,
                        &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"UsageV1", 0, REG_BINARY, (const BYTE *)persist, sizeof(*persist));
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, persist);
}

BOOL ys_storage_load_bool(const WCHAR *value_name, BOOL default_value) {
    HKEY key;
    DWORD type = 0;
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    if (!value_name || !value_name[0]) return default_value;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return default_value;
    if (RegQueryValueExW(key, value_name, NULL, &type, (BYTE *)&value, &bytes) != ERROR_SUCCESS ||
        type != REG_DWORD || bytes != sizeof(value)) {
        RegCloseKey(key);
        return default_value;
    }
    RegCloseKey(key);
    return value ? TRUE : FALSE;
}

void ys_storage_save_bool(const WCHAR *value_name, BOOL value) {
    HKEY key;
    DWORD disposition;
    DWORD data = value ? 1u : 0u;
    if (!value_name || !value_name[0]) return;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL,
                        &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, value_name, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
        RegCloseKey(key);
    }
}

void ys_search_history_init(YSSearchHistory *history) {
    if (history) ZeroMemory(history, sizeof(*history));
}

static void ys_search_history_copy(WCHAR *destination, const WCHAR *source) {
    StringCchCopyW(destination, YS_MAX_QUERY, source ? source : L"");
}

BOOL ys_search_history_add_front(YSSearchHistory *history, const WCHAR *query) {
    size_t index;
    size_t found = (size_t)-1;
    size_t last;
    if (!history || !query || !query[0]) return FALSE;
    for (index = 0; index < history->count; ++index) {
        if (_wcsicmp(history->items[index], query) == 0) {
            found = index;
            break;
        }
    }
    if (found == 0) return FALSE;
    if (found != (size_t)-1) {
        for (index = found; index > 0; --index) {
            ys_search_history_copy(history->items[index], history->items[index - 1]);
        }
    } else {
        last = history->count < YS_MAX_SEARCH_HISTORY ? history->count : YS_MAX_SEARCH_HISTORY - 1u;
        for (index = last; index > 0; --index) {
            ys_search_history_copy(history->items[index], history->items[index - 1]);
        }
        if (history->count < YS_MAX_SEARCH_HISTORY) ++history->count;
    }
    ys_search_history_copy(history->items[0], query);
    return TRUE;
}

void ys_storage_load_search_history(YSSearchHistory *history) {
    HKEY key;
    DWORD type = 0;
    DWORD bytes = 0;
    WCHAR *buffer;
    WCHAR *cursor;
    if (!history) return;
    ys_search_history_init(history);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
    if (RegQueryValueExW(key, L"SearchHistory", NULL, &type, NULL, &bytes) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ || bytes < sizeof(WCHAR) * 2u) {
        RegCloseKey(key);
        return;
    }
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes + sizeof(WCHAR) * 2u);
    if (buffer && RegQueryValueExW(key, L"SearchHistory", NULL, &type, (BYTE *)buffer, &bytes) == ERROR_SUCCESS) {
        for (cursor = buffer; *cursor && history->count < YS_MAX_SEARCH_HISTORY; cursor += wcslen(cursor) + 1u) {
            if (*cursor) {
                ys_search_history_copy(history->items[history->count], cursor);
                ++history->count;
            }
        }
    }
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    RegCloseKey(key);
}

void ys_storage_save_search_history(const YSSearchHistory *history) {
    HKEY key;
    DWORD disposition;
    size_t index;
    size_t total = 2u;
    size_t offset = 0;
    WCHAR *buffer;
    if (!history) return;
    for (index = 0; index < history->count; ++index) total += wcslen(history->items[index]) + 1u;
    buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total * sizeof(WCHAR));
    if (!buffer) return;
    for (index = 0; index < history->count; ++index) {
        size_t length = wcslen(history->items[index]);
        memcpy(buffer + offset, history->items[index], (length + 1u) * sizeof(WCHAR));
        offset += length + 1u;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, YS_REG_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL,
                        &key, &disposition) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"SearchHistory", 0, REG_MULTI_SZ, (const BYTE *)buffer,
                       (DWORD)(total * sizeof(WCHAR)));
        RegCloseKey(key);
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}
