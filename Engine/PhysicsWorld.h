#pragma once
#include <vector>
#include <memory>
#include <DirectXMath.h>

class Entity;
class ISystem;

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void Initialize();
    void Update(float deltaTime);
    void Shutdown();

    // Entity management
    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

    // System management
    void AddSystem(std::unique_ptr<ISystem> system);

    // Global physics properties
    void SetGravity(const DirectX::XMFLOAT3& gravity) { m_Gravity = gravity; }
    const DirectX::XMFLOAT3& GetGravity() const { return m_Gravity; }

    // Query functions
    std::vector<Entity*> GetEntitiesWithComponents(const std::vector<std::type_index>& componentTypes);

private:
    std::vector<Entity*> m_Entities;
    std::vector<std::unique_ptr<ISystem>> m_Systems;

    DirectX::XMFLOAT3 m_Gravity = { 0.0f, -9.81f, 0.0f };
    float m_FixedTimeStep = 1.0f / 60.0f;
};
