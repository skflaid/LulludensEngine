#include "PhysicsScheduler.h"

PhysicsScheduler::PhysicsScheduler(std::shared_ptr<PhysicsWorld> world)
    : m_PhysicsWorld(world) {
}

void PhysicsScheduler::Update(float deltaTime) {
    m_AccumulatedTime += deltaTime;

    while (m_AccumulatedTime >= m_FixedTimeStep) {
        FixedUpdate();
        m_AccumulatedTime -= m_FixedTimeStep;
    }
}

void PhysicsScheduler::SetFixedTimeStep(float timeStep) {
    m_FixedTimeStep = timeStep;
    if (m_PhysicsWorld) {
        // You might want to sync this with the world if needed
    }
}

void PhysicsScheduler::FixedUpdate() {
    if (m_PhysicsWorld) {
        m_PhysicsWorld->Update(m_FixedTimeStep);
    }
}
