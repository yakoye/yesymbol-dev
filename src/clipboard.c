#include "clipboard.h"
#include <wchar.h>
#include <string.h>

BOOL ys_clipboard_try_set(HWND owner, const WCHAR *text) {
    HGLOBAL memory;
    WCHAR *target;
    SIZE_T bytes;

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

    if (!OpenClipboard(owner)) {
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
