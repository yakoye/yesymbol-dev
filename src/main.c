#include "yesymbol.h"
#include "ui.h"
#include <commctrl.h>

typedef BOOL (WINAPI *SetDpiContextFn)(HANDLE);

static void ys_enable_dpi(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetDpiContextFn fn;
    if (!user32) return;
    fn = (SetDpiContextFn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (fn) fn((HANDLE)(LONG_PTR)-4);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command) {
    HANDLE mutex;
    HWND existing;
    INITCOMMONCONTROLSEX controls;
    int result;
    (void)previous;
    (void)command_line;

    ys_enable_dpi();
    mutex = CreateMutexW(NULL, FALSE, L"Local\\YeTools.YeSymbol.Singleton.v5");
    if (!mutex) return 2;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        existing = FindWindowW(YESYMBOL_WINDOW_CLASS, NULL);
        if (existing) {
            PostMessageW(existing, YESYMBOL_ACTIVATE_MESSAGE, 0, 0);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);
    result = ys_run_ui(instance, show_command);
    CloseHandle(mutex);
    return result;
}
