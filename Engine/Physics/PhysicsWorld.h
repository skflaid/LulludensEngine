// Physics/PhysicsWorld.h

#pragma once
#include "Core/Entity.h"
#include "Core/ISystem.h"
#include <vector>
#include <memory>
#include <DirectXMath.h>

using namespace DirectX;

class PhysicsWorld {
public:
    PhysicsWorld(); // 생성자 선언
    ~PhysicsWorld();

    void Initialize();
    void Update(float deltaTime);
    void Shutdown();

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

    void AddSystem(std::unique_ptr<ISystem> system);

    void SetGravity(const XMFLOAT3& gravity) { m_Gravity = gravity; }
    const XMFLOAT3& GetGravity() const { return m_Gravity; }

    float GetFixedTimeStep() const { return m_FixedTimeStep; }

private:
    std::vector<Entity*> m_Entities;
    std::vector<std::unique_ptr<ISystem>> m_Systems;

    XMFLOAT3 m_Gravity; // 중력 변수
    float m_FixedTimeStep;
};
