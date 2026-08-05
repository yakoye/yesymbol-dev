#pragma once
#include "yesymbol.h"
/* Attempts exactly once.  Clipboard contention must never stall the UI thread. */
BOOL ys_clipboard_try_set(HWND owner, const WCHAR *text);
