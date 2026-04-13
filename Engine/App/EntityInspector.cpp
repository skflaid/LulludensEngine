#include "EntityInspector.h"
#include "GameEngine.h"
#include "Core/Entity.h"
#include "Renderer/Components/TransformComponent.h" // 네가 쓰는 경로 그대로

#include <string>
#include <format> // C++20 가능하면 사용. 불가하면 swprintf 사용

// 레이아웃 상수
static const int PAD = 8;
static const int LIST_W = 200;
static const int ROW_H = 22;
static const int LABEL_W = 40;
static const int EDIT_W = 80;

bool EntityInspector::Create(HWND hParent, HINSTANCE hInst)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = EntityInspector::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"EntityInspectorWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc);

    // 우상단에 띄우고 싶다면 위치 조절
    m_hWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Entity Inspector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 360,
        hParent, nullptr, hInst, this);

    return m_hWnd != nullptr;
}

void EntityInspector::Show(int nCmdShow)
{
    ShowWindow(m_hWnd, nCmdShow);
}

void EntityInspector::BuildUI(HINSTANCE hInst)
{
    // 리스트
    m_hList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        PAD, PAD, LIST_W, 300, m_hWnd, (HMENU)IDC_EI_LIST, hInst, nullptr);

    // 라벨+에딧 유틸
    auto mkLabel = [&](int x, int y, const wchar_t* text) {
        return CreateWindow(L"STATIC", text, WS_CHILD | WS_VISIBLE,
            x, y, LABEL_W, ROW_H, m_hWnd, nullptr, hInst, nullptr);
        };
    auto mkEditRO = [&](int id, int x, int y) {
        return CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_READONLY,
            x, y, EDIT_W, ROW_H, m_hWnd, (HMENU)id, hInst, nullptr);
        };

    int baseX = PAD + LIST_W + PAD;
    int xL = baseX;
    int xE = baseX + LABEL_W + PAD;

    int y = PAD;

    // Position
    mkLabel(xL, y, L"Pos X"); m_hPosX = mkEditRO(IDC_EI_POSX, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Pos Y"); m_hPosY = mkEditRO(IDC_EI_POSY, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Pos Z"); m_hPosZ = mkEditRO(IDC_EI_POSZ, xE, y); y += ROW_H + 12;

    // Rotation
    mkLabel(xL, y, L"Rot X"); m_hRotX = mkEditRO(IDC_EI_ROTX, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Rot Y"); m_hRotY = mkEditRO(IDC_EI_ROTY, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Rot Z"); m_hRotZ = mkEditRO(IDC_EI_ROTZ, xE, y); y += ROW_H + 12;

    // Scale
    mkLabel(xL, y, L"Sca X"); m_hScaX = mkEditRO(IDC_EI_SCAX, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Sca Y"); m_hScaY = mkEditRO(IDC_EI_SCAY, xE, y); y += ROW_H + 4;
    mkLabel(xL, y, L"Sca Z"); m_hScaZ = mkEditRO(IDC_EI_SCAZ, xE, y); y += ROW_H + 4;
}

void EntityInspector::PopulateList(GameEngine* engine)
{
    SendMessage(m_hList, LB_RESETCONTENT, 0, 0);
    m_EntityCountCached = engine->GetEntityCount();

    for (uint64_t i = 0; i < m_EntityCountCached; ++i)
    {
        Entity* e = engine->GetEntityByIndex(i);
        if (!e) continue;
        wchar_t buf[64];
        swprintf_s(buf, L"Entity %u", e->GetID());
        SendMessage(m_hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

void EntityInspector::Update(GameEngine* engine)
{
    if (!m_hWnd) return;

    // 엔티티 개수 변하면 리스트 갱신
    const uint64_t countNow = engine->GetEntityCount();
    if (countNow != m_EntityCountCached)
    {
        PopulateList(engine);
    }

    // 선택된 항목의 트랜스폼만 갱신
    int sel = (int)SendMessage(m_hList, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR)
    {
        Entity* e = engine->GetEntityByIndex((uint64_t)sel);
        UpdateFields(e);
        m_LastSelection = sel;
    }
    else if (m_LastSelection != -1)
    {
        // 선택 해제되면 필드 비우기
        SetWindowText(m_hPosX, L""); SetWindowText(m_hPosY, L""); SetWindowText(m_hPosZ, L"");
        SetWindowText(m_hRotX, L""); SetWindowText(m_hRotY, L""); SetWindowText(m_hRotZ, L"");
        SetWindowText(m_hScaX, L""); SetWindowText(m_hScaY, L""); SetWindowText(m_hScaZ, L"");
        m_LastSelection = -1;
    }
}

void EntityInspector::UpdateFields(Entity* entity)
{
    if (!entity)
        return;

    auto* t = entity->GetComponent<TransformComponent>();
    if (!t) return;

    // 값 가져오기
    auto p = t->GetPosition();
    auto r = t->GetRotation();
    auto s = t->GetScale();

    wchar_t buf[64];
    swprintf_s(buf, L"%.3f", p.x); SetWindowText(m_hPosX, buf);
    swprintf_s(buf, L"%.3f", p.y); SetWindowText(m_hPosY, buf);
    swprintf_s(buf, L"%.3f", p.z); SetWindowText(m_hPosZ, buf);

    swprintf_s(buf, L"%.3f", r.x); SetWindowText(m_hRotX, buf);
    swprintf_s(buf, L"%.3f", r.y); SetWindowText(m_hRotY, buf);
    swprintf_s(buf, L"%.3f", r.z); SetWindowText(m_hRotZ, buf);

    swprintf_s(buf, L"%.3f", s.x); SetWindowText(m_hScaX, buf);
    swprintf_s(buf, L"%.3f", s.y); SetWindowText(m_hScaY, buf);
    swprintf_s(buf, L"%.3f", s.z); SetWindowText(m_hScaZ, buf);
}

LRESULT CALLBACK EntityInspector::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    EntityInspector* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<EntityInspector*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hWnd = hWnd;
    }
    else
    {
        self = reinterpret_cast<EntityInspector*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProc(hWnd, msg, wParam, lParam);

    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT EntityInspector::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        BuildUI((HINSTANCE)GetWindowLongPtr(m_hWnd, GWLP_HINSTANCE));
        return 0;

    case WM_SIZE:
    case WM_MOVE:
        // 단순툴이라 레이아웃 고정. 필요시 리사이즈 처리 추가.
        return 0;

    case WM_CLOSE:
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
        return 0;
    }
    return DefWindowProc(m_hWnd, msg, wParam, lParam);
}
