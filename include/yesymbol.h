#pragma once
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define YESYMBOL_PRODUCT_NAME L"符号大全"
#define YESYMBOL_VERSION L"1.0.3"
#define YESYMBOL_WINDOW_CLASS L"YeTools.YeSymbol.Window.v16"
#define YESYMBOL_GRID_CLASS L"YeTools.YeSymbol.Grid.v16"
#define YESYMBOL_RECENT_CLASS L"YeTools.YeSymbol.Recent.v5"
#define YESYMBOL_ACTIVATE_MESSAGE (WM_APP + 41)
#define YESYMBOL_TRAY_MESSAGE (WM_APP + 42)
#define YESYMBOL_DEFERRED_INIT_MESSAGE (WM_APP + 43)
#define YESYMBOL_DEFERRED_REFRESH_MESSAGE (WM_APP + 44)

#define YS_MAX_RECENT 64
#define YS_MAX_CUSTOM 128
#define YS_MAX_SEQUENCE 32
#define YS_MAX_QUERY 160
#define YS_MAX_SEARCH_HISTORY 32

#define YS_ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))
