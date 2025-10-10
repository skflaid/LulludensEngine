#pragma once
#include "ISystem.h"
#include "PhysicsWorld.h"

class DynamicsSystem : public ISystem {
public:
    DynamicsSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

    void Initialize() override {}
    void Update(float deltaTime) override;
    void Shutdown() override {}

    const char* GetName() const override { return "DynamicsSystem"; }

private:
    PhysicsWorld* m_PhysicsWorld;

    void ApplyForces(float deltaTime);
    void IntegrateVelocity(float deltaTime);
    void IntegratePosition(float deltaTime);
};
