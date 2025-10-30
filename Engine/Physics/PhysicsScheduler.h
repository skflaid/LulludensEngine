#pragma once
#include "PhysicsWorld.h"
#include <memory>

class PhysicsScheduler {
public:
    PhysicsScheduler(std::shared_ptr<PhysicsWorld> world);

    void Update(float deltaTime);

    void SetFixedTimeStep(float timeStep);
    float GetFixedTimeStep() const { return m_FixedTimeStep; }

private:
    void FixedUpdate();

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    float m_FixedTimeStep = 1.0f / 60.0f;
    float m_AccumulatedTime = 0.0f;
};
