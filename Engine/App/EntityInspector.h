#pragma once

#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <cstdint>

class GameEngine;
class Entity;

class EntityInspector
{
public:
    bool Create(HWND hParent, HINSTANCE hInst);
    void Show(int nCmdShow);
    void Update(GameEngine* engine); // 매 프레임 혹은 필요시 호출

private:
    // 윈도우 / 컨트롤
    HWND m_hWnd = nullptr;
    HWND m_hList = nullptr; // 엔티티 리스트
    HWND m_hPosX = nullptr, m_hPosY = nullptr, m_hPosZ = nullptr;
    HWND m_hRotX = nullptr, m_hRotY = nullptr, m_hRotZ = nullptr;
    HWND m_hScaX = nullptr, m_hScaY = nullptr, m_hScaZ = nullptr;

    int  m_LastSelection = -1;      // 리스트 선택 인덱스
    uint64_t m_EntityCountCached = 0; // 엔티티 수 캐시(변화시 리스트 갱신)

    void BuildUI(HINSTANCE hInst);
    void PopulateList(GameEngine* engine);
    void UpdateFields(Entity* entity); // 선택 엔티티의 트랜스폼 갱신

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT, WPARAM, LPARAM);
};

// 컨트롤 ID (define 선호한다고 해서 매크로로 정의)
#define IDC_EI_LIST   1001
#define IDC_EI_POSX   1101
#define IDC_EI_POSY   1102
#define IDC_EI_POSZ   1103
#define IDC_EI_ROTX   1201
#define IDC_EI_ROTY   1202
#define IDC_EI_ROTZ   1203
#define IDC_EI_SCAX   1301
#define IDC_EI_SCAY   1302
#define IDC_EI_SCAZ   1303
