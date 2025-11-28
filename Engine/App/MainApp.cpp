#include <Windows.h>
#include <cstdint>
#include "GameEngine.h"
#include "EntityInspector.h"
#include "Core/InputManager.h"

// ���� �������� ���� (�ٸ� ��⿡�� ������ �� �����Ƿ� �̸� �״��)
GameEngine g_Engine;
EntityInspector g_Inspector;
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
    case WM_KEYDOWN:
        if (wParam == 'I' || wParam == 'i') {
            // I키를 눌렀을 때 렌더링 모드 토글
            g_Engine.ToggleRenderMode();
        }
        else if (wParam == VK_ESCAPE) {
            // ESC키로 마우스 해제
            InputManager::Get()->ReleaseMouse();
        }
        return 0;
    case WM_LBUTTONDOWN:
        // 왼쪽 마우스 클릭 시 마우스 캡처
        InputManager::Get()->CaptureMouse();
        return 0;
    case WM_RBUTTONDOWN:
        // 오른쪽 마우스 클릭 시 마우스 해제
        InputManager::Get()->ReleaseMouse();
        return 0;
    case WM_ACTIVATE:
        // 창 포커스 잃으면 마우스 해제
        if (LOWORD(wParam) == WA_INACTIVE) {
            InputManager::Get()->ReleaseMouse();
        }
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
    g_Inspector.Create(g_Hwnd, hInstance);
    g_Inspector.Show(SW_SHOW);

    // InputManager에 윈도우 핸들 전달
    InputManager::Get()->SetWindowHandle(g_Hwnd);

    if (!g_Engine.Initialize(g_Hwnd, WINDOW_WIDTH, WINDOW_HEIGHT))
        return -1;

    // ���� ����
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

        g_Inspector.Update(&g_Engine);
    }

    g_Engine.Shutdown();
    return 0;
}
