#include "InputManager.h"

// 싱글톤 인스턴스 초기화
std::unique_ptr<InputManager> InputManager::s_Instance = nullptr;

// 싱글톤 인스턴스 생성 및 반환
InputManager* InputManager::Get() {
    if (s_Instance == nullptr) {
        s_Instance = std::unique_ptr<InputManager>(new InputManager());
    }
    return s_Instance.get();
}

// 생성자
InputManager::InputManager() {
    // 초기 마우스 위치 설정
    if (m_IsFirstMouseUpdate)
    {
        GetCursorPos(&m_LastMousePos);
        m_IsFirstMouseUpdate = false;
    }
}

// 매 프레임 업데이트 함수
void InputManager::Update() {
    // 1. 마우스 델타 값 계산 (기존 코드)
    POINT currentPos;
    if (GetCursorPos(&currentPos)) {
        // 첫 업데이트 시 델타 값이 너무 크게 튀는 것을 방지
        if (m_IsFirstMouseUpdate) {
            m_LastMousePos = currentPos;
            m_IsFirstMouseUpdate = false;
        }

        m_MouseDelta.x = currentPos.x - m_LastMousePos.x;
        m_MouseDelta.y = currentPos.y - m_LastMousePos.y;

        // 마우스 커서를 화면 중앙으로 고정 (FPS 카메라 스타일)
        // 이 기능을 사용하려면 GameEngine 등에서 화면의 너비와 높이를 알아야 합니다.
        // HWND hwnd = GetActiveWindow();
        // RECT rect;
        // GetWindowRect(hwnd, &rect);
        // int centerX = rect.left + (rect.right - rect.left) / 2;
        // int centerY = rect.top + (rect.bottom - rect.top) / 2;
        // SetCursorPos(centerX, centerY);
        // m_LastMousePos = { centerX, centerY };

        // 일단 커서 고정 없이 진행, 마지막 위치만 갱신
        m_LastMousePos = currentPos;
    }
    else {
        m_MouseDelta = { 0, 0 };
    }

    // 2. 키보드 상태 갱신 (새로 추가된 코드)
    // 256개의 모든 가상 키 상태를 확인합니다.
    for (int i = 0; i < 256; ++i) {
        // GetAsyncKeyState 함수는 키의 현재 눌림 상태와, 마지막 호출 이후 눌렸었는지 여부를 반환합니다.
        // 최상위 비트(0x8000)가 1이면 현재 키가 눌려있는 상태입니다.
        if (GetAsyncKeyState(i) & 0x8000) {
            m_KeyStates[i] = true;
        }
        else {
            m_KeyStates[i] = false;
        }
    }
}

// 특정 키가 눌렸는지 확인하는 함수
bool InputManager::IsKeyPressed(int vKey) {
    // 맵에 키가 존재하고, 그 값이 true(눌림)이면 true 반환
    if (m_KeyStates.count(vKey)) {
        return m_KeyStates[vKey];
    }
    return false;
}

// 마우스 이동량을 반환하는 함수
POINT InputManager::GetMouseDelta() {
    return m_MouseDelta;
}

// WinProc에서 마우스 위치를 설정하는 함수 (필요 시 사용)
// 현재 구현에서는 GetCursorPos를 사용하므로 이 함수는 사용되지 않음.
void InputManager::SetMousePosition(int x, int y) {
    // 이 함수를 사용하려면 Update 로직을 수정해야 함.
    // m_LastMousePos = { x, y }; 
}