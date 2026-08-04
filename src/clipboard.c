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

    /* OpenClipboard transiently fails while another process (Windows
     * Clipboard History, Cloud Clipboard sync, a clipboard manager, etc.)
     * briefly opens the clipboard right after we write to it. This function
     * runs synchronously on the UI thread inside the mouse-click handler, so
     * Sleep() here stalls the message loop -- and Sleep(N) is rounded up to
     * a full scheduler tick (commonly ~15ms) on most systems, so the old
     * fixed 8 x Sleep(5) loop could block the UI thread for 100ms+ on a
     * single click, which is exactly what made rapid clicking feel like it
     * was dropping input. SwitchToThread() yields the rest of this thread's
     * timeslice without that floor, so most transient contention clears
     * within one or two (near-zero-cost) yields; only genuinely persistent
     * contention falls through to a few minimal Sleep(1) attempts. */
    for (attempt = 0; attempt < 50; ++attempt) {
        if (OpenClipboard(owner)) break;
        if (attempt < 40) SwitchToThread();
        else Sleep(1);
    }
    if (attempt == 50) {
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
