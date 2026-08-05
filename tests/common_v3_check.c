#include "storage.h"
#include <assert.h>
#include <stdio.h>
#include <wchar.h>

static void expect_item(const YSCommonList *list, size_t index, const WCHAR *text, uint32_t origin) {
    assert(index < list->count);
    assert(wcscmp(list->items[index].text, text) == 0);
    assert(list->items[index].origin == origin);
}

static void test_fresh_defaults_and_user_items(void) {
    static const WCHAR *defaults[] = {L"A", L"B", L"C"};
    YSCommonList list;

    ys_common_init(&list);
    assert(ys_common_reconcile(&list, defaults, 3u));
    assert(list.count == 3u);
    expect_item(&list, 0u, L"A", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 1u, L"B", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 2u, L"C", YS_COMMON_ORIGIN_DEFAULT);

    assert(ys_common_add_user(&list, L"X", 5u));
    assert(list.count == 4u);
    expect_item(&list, 3u, L"X", YS_COMMON_ORIGIN_USER);

    assert(ys_common_remove(&list, 1u));
    assert(ys_common_is_suppressed(&list, L"B"));
    assert(!ys_common_reconcile(&list, defaults, 3u));
    assert(list.count == 3u);
    expect_item(&list, 0u, L"A", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 1u, L"C", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 2u, L"X", YS_COMMON_ORIGIN_USER);

    assert(ys_common_add_user(&list, L"B", 7u));
    assert(!ys_common_is_suppressed(&list, L"B"));
    assert(list.count == 4u);
    expect_item(&list, 3u, L"B", YS_COMMON_ORIGIN_USER);
    assert(!ys_common_reconcile(&list, defaults, 3u));
    assert(list.count == 4u);
}

static void test_new_and_removed_defaults(void) {
    static const WCHAR *old_defaults[] = {L"A", L"B", L"C"};
    static const WCHAR *new_defaults[] = {L"B", L"C", L"D"};
    YSCommonList list;

    ys_common_init(&list);
    assert(ys_common_reconcile(&list, old_defaults, 3u));
    assert(ys_common_move(&list, 1u, 0u));
    assert(ys_common_add_user(&list, L"X", 9u));
    assert(ys_common_reconcile(&list, new_defaults, 3u));
    assert(list.count == 4u);
    expect_item(&list, 0u, L"B", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 1u, L"C", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 2u, L"D", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 3u, L"X", YS_COMMON_ORIGIN_USER);
}

static void test_v2_migration_preserves_absence_and_learned_items(void) {
    static const WCHAR *defaults[] = {L"A", L"B", L"C"};
    YSCommonList list;

    ys_common_init(&list);
    assert(ys_common_add_user(&list, L"B", 4u));
    assert(ys_common_add_user(&list, L"X", 8u));
    list.loaded_version = 2u;
    assert(ys_common_reconcile(&list, defaults, 3u));
    assert(list.count == 2u);
    expect_item(&list, 0u, L"B", YS_COMMON_ORIGIN_DEFAULT);
    expect_item(&list, 1u, L"X", YS_COMMON_ORIGIN_USER);
    assert(ys_common_is_suppressed(&list, L"A"));
    assert(ys_common_is_suppressed(&list, L"C"));
    assert(list.loaded_version == 3u);
}

static void test_capacity_and_deduplication(void) {
    YSCommonList list;
    WCHAR text[16];
    size_t index;

    ys_common_init(&list);
    for (index = 0; index < YS_MAX_COMMON; ++index) {
        _snwprintf_s(text, YS_ARRAY_COUNT(text), _TRUNCATE, L"U%03u", (unsigned)index);
        assert(ys_common_add_user(&list, text, 0u));
    }
    assert(!ys_common_add_user(&list, L"overflow", 0u));
    assert(!ys_common_add_user(&list, L"U000", 0u));
    assert(list.count == YS_MAX_COMMON);
}

int wmain(void) {
    test_fresh_defaults_and_user_items();
    test_new_and_removed_defaults();
    test_v2_migration_preserves_absence_and_learned_items();
    test_capacity_and_deduplication();
    wprintf(L"CommonV3 checks passed\n");
    return 0;
}
