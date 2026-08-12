#pragma once

#include <Windows.h>
#include <unordered_map>
#include <memory>

class InputManager {
public:
    // 싱글턴 인스턴스를 가져오는 함수
    static InputManager* Get();

    // 매 프레임 호출하여 마우스 이동량 등을 갱신
    void Update();

    // 특정 키가 현재 눌려있는지 확인
    bool IsKeyPressed(int vKey);

    // 마우스가 마지막 프레임 이후 얼마나 움직였는지 반환
    POINT GetMouseDelta();

    // WinProc에서 호출될 함수들
    void SetKeyState(int vKey, bool isPressed);
    void SetMousePosition(int x, int y);
    void SetGameplayInputEnabled(bool enabled);

    // 마우스 캡처 관련 함수들 (FPS 카메라용)
    void SetWindowHandle(HWND hwnd);
    void CaptureMouse();   // 마우스 숨기고 창에 고정
    void ReleaseMouse();   // 마우스 복원
    bool IsCaptured() const { return m_MouseCaptured; }

    // 소멸자에서 인스턴스 해제
    ~InputManager();

private:
    // 생성자를 private으로 막아 외부 생성을 방지
    InputManager();

    // 복사 및 대입을 방지
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // 싱글턴 인스턴스
    static std::unique_ptr<InputManager> s_Instance;

    // 키 상태를 저장하는 맵
    std::unordered_map<int, bool> m_KeyStates;

    // 마우스 위치 관련 변수
    POINT m_LastMousePos = { 0, 0 };
    POINT m_MouseDelta = { 0, 0 };
    bool m_IsFirstMouseUpdate = true;

    // 마우스 캡처 관련
    HWND m_Hwnd = nullptr;
    bool m_MouseCaptured = false;
    bool m_GameplayInputEnabled = true;
};
