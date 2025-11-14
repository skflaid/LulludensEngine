#pragma once
#include "Core/ISystem.h"

class GameEngine; // 전방 선언은 그대로 둡니다.

class CameraSystem : public ISystem {
public:
    // 생성자와 소멸자를 헤더에서는 선언만 합니다.
    CameraSystem();
    ~CameraSystem();

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;
    const char* GetName() const override;

    void SetGameEngine(GameEngine* engine);

private:
    GameEngine* m_Engine = nullptr;
};