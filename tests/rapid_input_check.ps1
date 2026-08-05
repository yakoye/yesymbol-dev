$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class RapidNative {
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)] private struct GUITHREADINFO {
        public int cbSize; public uint flags; public IntPtr hwndActive; public IntPtr hwndFocus;
        public IntPtr hwndCapture; public IntPtr hwndMenuOwner; public IntPtr hwndMoveSize; public IntPtr hwndCaret;
        public RECT rcCaret;
    }
    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(IntPtr className, string title);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)]
    public static extern IntPtr SendMessageText(IntPtr hwnd, uint msg, IntPtr w, StringBuilder text);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
    [DllImport("user32.dll")] private static extern bool GetGUIThreadInfo(uint threadId, ref GUITHREADINFO info);
    public static void ClickClient(IntPtr hwnd, int x, int y) {
        POINT point = new POINT { x = x, y = y };
        ClientToScreen(hwnd, ref point);
        SetCursorPos(point.x, point.y);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }
    public static IntPtr GetFocusedControl(IntPtr hwnd) {
        uint thread = GetWindowThreadProcessId(hwnd, IntPtr.Zero);
        GUITHREADINFO info = new GUITHREADINFO();
        info.cbSize = Marshal.SizeOf(info);
        return GetGUIThreadInfo(thread, ref info) ? info.hwndFocus : IntPtr.Zero;
    }
}
'@
[RapidNative]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null

$root = Split-Path -Parent $PSScriptRoot
$receiverPath = Join-Path $root 'build\Release\rapid_input_receiver.exe'
$appPath = Join-Path $root 'dist\yesymbol.exe'
if (!(Test-Path $receiverPath) -or !(Test-Path $appPath)) {
    throw 'Build rapid_input_receiver and yesymbol before running this check.'
}

Get-Process yesymbol, rapid_input_receiver -ErrorAction SilentlyContinue | Stop-Process -Force
$receiver = Start-Process $receiverPath -PassThru
$app = $null
try {
    $receiverMain = [IntPtr]::Zero
    for ($i = 0; $receiverMain -eq [IntPtr]::Zero -and $i -lt 100; ++$i) {
        Start-Sleep -Milliseconds 20
        $receiverMain = [RapidNative]::FindWindowW([IntPtr]::Zero, 'YeSymbol Rapid Input Receiver')
    }
    if ($receiverMain -eq [IntPtr]::Zero) { throw 'Receiver window did not start.' }

    $app = Start-Process $appPath -WorkingDirectory (Split-Path $appPath) -PassThru
    $main = [IntPtr]::Zero
    for ($i = 0; $main -eq [IntPtr]::Zero -and $i -lt 150; ++$i) {
        Start-Sleep -Milliseconds 20
        $main = [RapidNative]::FindWindowW([IntPtr]::Zero, '符号大全 - 添加符号')
    }
    if ($main -eq [IntPtr]::Zero) { throw 'YeSymbol window did not start.' }

    $autoInsert = $categories = $grid = [IntPtr]::Zero
    for ($i = 0; $grid -eq [IntPtr]::Zero -and $i -lt 100; ++$i) {
        Start-Sleep -Milliseconds 20
        $autoInsert = [RapidNative]::GetDlgItem($main, 1005)
        $categories = [RapidNative]::GetDlgItem($main, 1003)
        $grid = [RapidNative]::GetDlgItem($main, 1004)
    }
    if ($autoInsert -eq [IntPtr]::Zero -or $categories -eq [IntPtr]::Zero -or $grid -eq [IntPtr]::Zero) {
        throw 'Required YeSymbol controls were not created.'
    }

    [RapidNative]::SendMessage($autoInsert, 0x00F1, [IntPtr]1, [IntPtr]::Zero) | Out-Null # BM_SETCHECK
    [RapidNative]::SendMessage($main, 0x0111, [IntPtr]1005, $autoInsert) | Out-Null       # WM_COMMAND
    [RapidNative]::SendMessage($categories, 0x0186, [IntPtr]3, [IntPtr]::Zero) | Out-Null # LB_SETCURSEL 序号字母
    [RapidNative]::SendMessage($main, 0x0111, [IntPtr](1003 -bor (1 -shl 16)), $categories) | Out-Null

    $receiverHwnd = $receiverMain
    $edit = [RapidNative]::GetDlgItem($receiverHwnd, 1)
    $foregroundThread = [RapidNative]::GetWindowThreadProcessId([RapidNative]::GetForegroundWindow(), [IntPtr]::Zero)
    $currentThread = [RapidNative]::GetCurrentThreadId()
    [RapidNative]::AttachThreadInput($currentThread, $foregroundThread, $true) | Out-Null
    [RapidNative]::BringWindowToTop($receiverHwnd) | Out-Null
    [RapidNative]::SetForegroundWindow($receiverHwnd) | Out-Null
    [RapidNative]::SetFocus($edit) | Out-Null
    [RapidNative]::AttachThreadInput($currentThread, $foregroundThread, $false) | Out-Null
    if ([RapidNative]::GetForegroundWindow() -ne $receiverHwnd) {
        [RapidNative]::SetWindowPos($receiverHwnd, [IntPtr](-1), 830, 40, 600, 100, 0x0040) | Out-Null
        [RapidNative]::SetCursorPos(900, 90) | Out-Null
        [RapidNative]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [RapidNative]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    }
    [RapidNative]::SendMessage($receiverHwnd, 0x8001, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 350 # allow YeSymbol's target poll to observe the receiver
    if ([RapidNative]::GetForegroundWindow() -ne $receiverHwnd) {
        $foreground = [RapidNative]::GetForegroundWindow()
        $foregroundTitle = New-Object Text.StringBuilder 256
        [RapidNative]::GetWindowText($foreground, $foregroundTitle, $foregroundTitle.Capacity) | Out-Null
        throw "Receiver could not become the foreground window (receiver=$receiverHwnd foreground=$foreground '$foregroundTitle')."
    }
    [RapidNative]::SendMessage($main, 0x0113, [IntPtr]1, [IntPtr]::Zero) | Out-Null # deterministic foreground poll
    $expected = '①②③④⑤⑥⑦⑧⑨⑩⑪⑫⑬⑭⑮⑯⑰⑱⑲⑳'
    foreach ($char in $expected.ToCharArray()) {
        [RapidNative]::PostMessageW($edit, 0x0102, [IntPtr][int]$char, [IntPtr]1) | Out-Null
    }
    Start-Sleep -Milliseconds 200
    $fixture = New-Object Text.StringBuilder 128
    [RapidNative]::SendMessageText($edit, 0x000D, [IntPtr]$fixture.Capacity, $fixture) | Out-Null
    if ($fixture.ToString() -ne $expected) { throw "Receiver WM_CHAR fixture failed: '$fixture'." }
    [RapidNative]::SendMessage($edit, 0x000C, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    foreach ($row in 0..1) {
        foreach ($column in 0..9) {
            $x = 26 + 46 * $column
            $y = 25 + 52 * $row
            $lparam = [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
            [RapidNative]::SendMessage($grid, 0x0202, [IntPtr]::Zero, $lparam) | Out-Null
            Start-Sleep -Milliseconds 2
        }
    }
    Start-Sleep -Milliseconds 300

    $actual = New-Object Text.StringBuilder 128
    [RapidNative]::SendMessageText($edit, 0x000D, [IntPtr]$actual.Capacity, $actual) | Out-Null # WM_GETTEXT
    if ($actual.ToString() -ne $expected) {
        $selected = [RapidNative]::SendMessage($categories, 0x0188, [IntPtr]::Zero, [IntPtr]::Zero)
        $autoState = [RapidNative]::SendMessage($autoInsert, 0x00F0, [IntPtr]::Zero, [IntPtr]::Zero)
        $focused = [RapidNative]::GetFocusedControl($receiverHwnd)
        $statusText = New-Object Text.StringBuilder 512
        [RapidNative]::GetWindowText([RapidNative]::GetDlgItem($main, 1010), $statusText, $statusText.Capacity) | Out-Null
        throw "Rapid input mismatch. Expected '$expected', got '$actual' (category=$selected auto=$autoState edit=$edit focus=$focused status='$statusText')."
    }
    Write-Host "rapid input check passed: $actual"
}
finally {
    if ($app -and !$app.HasExited) { $app.Kill() }
    if (!$receiver.HasExited) { $receiver.Kill() }
}
