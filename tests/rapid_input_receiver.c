#define UNICODE
#define _UNICODE
#include <windows.h>

#define RECEIVER_EDIT_ID 1

static LRESULT CALLBACK receiver_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                    12, 12, 560, 34, hwnd,
                                    (HMENU)(INT_PTR)RECEIVER_EDIT_ID,
                                    ((CREATESTRUCTW *)lparam)->hInstance, NULL);
        if (!edit) return -1;
        SendMessageW(edit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SetFocus(edit);
        return 0;
    }
    case WM_SETFOCUS:
        SetFocus(GetDlgItem(hwnd, RECEIVER_EDIT_ID));
        return 0;
    case WM_APP + 1:
        SetForegroundWindow(hwnd);
        SetFocus(GetDlgItem(hwnd, RECEIVER_EDIT_ID));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show_command) {
    WNDCLASSEXW window_class;
    HWND hwnd;
    MSG message;
    (void)previous;
    (void)command_line;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = receiver_proc;
    window_class.lpszClassName = L"YeSymbol.RapidInputReceiver";
    window_class.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    if (!RegisterClassExW(&window_class)) return 1;
    hwnd = CreateWindowExW(0, window_class.lpszClassName,
                           L"YeSymbol Rapid Input Receiver",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                           830, 40, 600, 100, NULL, NULL, instance, NULL);
    if (!hwnd) return 2;
    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
