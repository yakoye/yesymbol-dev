#include "yesymbol.h"
#include "about.h"
#include "about_config.h"
#include "resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <wchar.h>

static HRESULT CALLBACK ys_about_callback(HWND hwnd, UINT notification, WPARAM wparam, LPARAM lparam, LONG_PTR data) {
    (void)hwnd;
    (void)wparam;
    (void)data;
    if (notification == TDN_HYPERLINK_CLICKED && lparam) {
        const WCHAR *target = (const WCHAR *)lparam;
        ShellExecuteW(NULL, L"open", target, NULL, NULL, SW_SHOWNORMAL);
    }
    return S_OK;
}

void ys_show_about_dialog(HWND owner, HINSTANCE instance) {
    TASKDIALOGCONFIG config;
    WCHAR content[1800];
    WCHAR footer[256];
    const BOOL has_web = YESYMBOL_WEB_URL[0] != L'\0';

    if (has_web) {
        StringCchPrintfW(content, ARRAYSIZE(content),
            L"版本：%s\n"
            L"开发日期：%s\n\n"
            L"开发目的：\n%s\n\n"
            L"联系作者：<a href=\"mailto:%s\">%s</a>\n"
            L"github：<a href=\"%s\">%s</a>\n"
            L"网页版：<a href=\"%s\">%s</a>",
            YESYMBOL_VERSION,
            YESYMBOL_DEVELOPMENT_DATE,
            YESYMBOL_DEVELOPMENT_PURPOSE,
            YESYMBOL_AUTHOR_EMAIL,
            YESYMBOL_AUTHOR_EMAIL,
            YESYMBOL_GITHUB_URL,
            YESYMBOL_GITHUB_LABEL,
            YESYMBOL_WEB_URL,
            YESYMBOL_WEB_LABEL);
    } else {
        StringCchPrintfW(content, ARRAYSIZE(content),
            L"版本：%s\n"
            L"开发日期：%s\n\n"
            L"开发目的：\n%s\n\n"
            L"联系作者：<a href=\"mailto:%s\">%s</a>\n"
            L"github：<a href=\"%s\">%s</a>\n"
            L"网页版：开发中，敬请期待。",
            YESYMBOL_VERSION,
            YESYMBOL_DEVELOPMENT_DATE,
            YESYMBOL_DEVELOPMENT_PURPOSE,
            YESYMBOL_AUTHOR_EMAIL,
            YESYMBOL_AUTHOR_EMAIL,
            YESYMBOL_GITHUB_URL,
            YESYMBOL_GITHUB_LABEL);
    }

    StringCchCopyW(footer, ARRAYSIZE(footer),
        L"C11 主程序 / Win32 API / DirectWrite · 无需安装 · 不依赖 .NET");

    ZeroMemory(&config, sizeof(config));
    config.cbSize = sizeof(config);
    config.hwndParent = owner;
    config.hInstance = instance;
    config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = L"关于 YeSymbol";
    config.pszMainInstruction = L"YeSymbol（符号大全）";
    config.pszContent = content;
    config.pszFooter = footer;
    config.pszMainIcon = MAKEINTRESOURCEW(IDI_YESYMBOL);
    config.pfCallback = ys_about_callback;
    TaskDialogIndirect(&config, NULL, NULL, NULL);
}
