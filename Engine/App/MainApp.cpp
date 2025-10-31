#include <Windows.h>
#include <cstdint>
#include "GameEngine.h"

// 기존 전역변수 유지 (다른 모듈에서 참조할 수 있으므로 이름 그대로)
GameEngine g_Engine;
HWND g_Hwnd = nullptr;
bool g_Running = true;

// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        g_Running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"GameEngineWindowClass";
    const uint32_t WINDOW_WIDTH = 1280;
    const uint32_t WINDOW_HEIGHT = 720;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassEx(&wc);

    RECT wr = { 0, 0, (LONG)WINDOW_WIDTH, (LONG)WINDOW_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    g_Hwnd = CreateWindowEx(
        0, CLASS_NAME,
        L"Game Engine - Physics + Renderer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_Hwnd) return -1;

    ShowWindow(g_Hwnd, nCmdShow);

    if (!g_Engine.Initialize(g_Hwnd, WINDOW_WIDTH, WINDOW_HEIGHT))
        return -1;

    // 메인 루프
    MSG msg = {};
    LARGE_INTEGER freq, last, cur;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    while (g_Running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        QueryPerformanceCounter(&cur);
        float dt = static_cast<float>(cur.QuadPart - last.QuadPart) / freq.QuadPart;
        last = cur;

        g_Engine.Update(dt);
        g_Engine.Render();
    }

    g_Engine.Shutdown();
    return 0;
}
