#include "clipboard.h"
#include <wchar.h>
#include <string.h>

BOOL ys_clipboard_set(HWND owner, const WCHAR *text) {
    HGLOBAL memory;
    WCHAR *target;
    SIZE_T bytes;
    int attempt;

    if (!text) return FALSE;
    bytes = (wcslen(text) + 1u) * sizeof(WCHAR);
    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return FALSE;
    target = (WCHAR *)GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return FALSE;
    }
    memcpy(target, text, bytes);
    GlobalUnlock(memory);

    for (attempt = 0; attempt < 8; ++attempt) {
        if (OpenClipboard(owner)) break;
        Sleep(5);
    }
    if (attempt == 8) {
        GlobalFree(memory);
        return FALSE;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}
