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
    // 초기화는 Update에서 처리
}

// 매 프레임 업데이트 함수
void InputManager::Update() {
    // 1. 마우스 델타 값 계산
    POINT currentPos;
    if (GetCursorPos(&currentPos)) {
        // 첫 업데이트 시 델타 값이 너무 크게 튀는 것을 방지
        if (m_IsFirstMouseUpdate) {
            m_LastMousePos = currentPos;
            m_IsFirstMouseUpdate = false;
        }

        m_MouseDelta.x = currentPos.x - m_LastMousePos.x;
        m_MouseDelta.y = currentPos.y - m_LastMousePos.y;

        // 마우스 캡처 상태일 경우 커서를 화면 중앙으로 고정
        if (m_MouseCaptured && m_Hwnd) {
            RECT rect;
            GetClientRect(m_Hwnd, &rect);
            ClientToScreen(m_Hwnd, (POINT*)&rect.left);
            ClientToScreen(m_Hwnd, (POINT*)&rect.right);

            int centerX = (rect.left + rect.right) / 2;
            int centerY = (rect.top + rect.bottom) / 2;
            SetCursorPos(centerX, centerY);
            m_LastMousePos = { centerX, centerY };
        }
        else {
            // 캡처 상태가 아니면 마지막 위치만 갱신
            m_LastMousePos = currentPos;
        }
    }
    else {
        m_MouseDelta = { 0, 0 };
    }

    // 2. 키보드 상태 갱신
    for (int i = 0; i < 256; ++i) {
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
    if (m_KeyStates.count(vKey)) {
        return m_KeyStates[vKey];
    }
    return false;
}

// 마우스 이동량을 반환하는 함수
POINT InputManager::GetMouseDelta() {
    return m_MouseDelta;
}

void InputManager::SetMousePosition(int x, int y) {
    m_LastMousePos = { x, y };
}

void InputManager::SetKeyState(int vKey, bool isPressed) {
    m_KeyStates[vKey] = isPressed;
}

// === 마우스 캡처 기능 구현 ===

void InputManager::SetWindowHandle(HWND hwnd) {
    m_Hwnd = hwnd;
}

void InputManager::CaptureMouse() {
    if (!m_Hwnd || m_MouseCaptured) return;

    // 1. 커서 숨기기
    while (ShowCursor(FALSE) >= 0);

    // 2. 커서를 창 영역에 고정
    RECT rect;
    GetClientRect(m_Hwnd, &rect);
    ClientToScreen(m_Hwnd, (POINT*)&rect.left);
    ClientToScreen(m_Hwnd, (POINT*)&rect.right);
    ClipCursor(&rect);

    // 3. 초기 위치를 화면 중앙으로
    int centerX = (rect.left + rect.right) / 2;
    int centerY = (rect.top + rect.bottom) / 2;
    SetCursorPos(centerX, centerY);
    m_LastMousePos = { centerX, centerY };

    m_MouseCaptured = true;
    m_IsFirstMouseUpdate = true;
}

void InputManager::ReleaseMouse() {
    if (!m_MouseCaptured) return;

    // 1. 커서 복원
    while (ShowCursor(TRUE) < 0);

    // 2. 클리핑 해제
    ClipCursor(nullptr);

    m_MouseCaptured = false;
}

InputManager::~InputManager() {
    ReleaseMouse();
}