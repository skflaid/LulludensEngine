#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Physics/PhysicsWorld.h"
#include <vector>

class DynamicsSystem : public ISystem {
public:
    DynamicsSystem(PhysicsWorld* world);

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;

    const char* GetName() const override { return "DynamicsSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

private:
    void ApplyGravity(Entity* entity, float deltaTime);
    void ApplyForces(Entity* entity, float deltaTime);
    void IntegrateVelocity(Entity* entity, float deltaTime);
    void IntegratePosition(Entity* entity, float deltaTime);

private:
    PhysicsWorld* m_PhysicsWorld;
    std::vector<Entity*> m_Entities;
};
