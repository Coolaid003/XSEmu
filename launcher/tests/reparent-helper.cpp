// A tiny original Win32 program for tst_reparent: one top-level window painted a solid colour that
// exits with code 0 when it is closed. It stands in for a game window a runner opens, so the test can
// reparent a real window from another process into a Qt host.
#include <windows.h>

static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        HBRUSH brush = CreateSolidBrush(RGB(36, 99, 235));
        FillRect(dc, &ps.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"XSEmuReparentHelper";
    if (!RegisterClassW(&wc))
        return 2;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Reparent helper", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 360,
                                  nullptr, nullptr, instance, nullptr);
    if (!window)
        return 3;
    // the first ShowWindow may take its command from the parent's STARTUPINFO; the second one is ours
    ShowWindow(window, SW_SHOWNOACTIVATE);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}
