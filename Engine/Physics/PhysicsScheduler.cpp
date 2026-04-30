#include "PhysicsScheduler.h"
#include "Threading/SnapshotBuffer.h"
#include <algorithm>

PhysicsScheduler::PhysicsScheduler(std::shared_ptr<PhysicsWorld> world)
    : m_PhysicsWorld(world) {
}

uint32_t PhysicsScheduler::Update(float deltaTime) {
    // Physics can be called from the worker thread or single-thread fallback.
    // Accumulation decouples simulation stability from render frame timing.
    m_AccumulatedTime += deltaTime;

    uint32_t fixedStepCount = 0;
    while (m_AccumulatedTime >= m_FixedTimeStep) {
        // Catch up one deterministic tick at a time.
        FixedUpdate();
        ++fixedStepCount;
        m_AccumulatedTime -= m_FixedTimeStep;
    }

    return fixedStepCount;
}

void PhysicsScheduler::SetFixedTimeStep(float timeStep) {
    m_FixedTimeStep = timeStep;
    if (m_PhysicsWorld) {
        // You might want to sync this with the world if needed
    }
}

float PhysicsScheduler::GetInterpolationAlpha() const {
    if (m_FixedTimeStep <= 0.0f) {
        return 0.0f;
    }

    // Remaining accumulated time is the render blend factor into the next tick.
    return std::clamp(m_AccumulatedTime / m_FixedTimeStep, 0.0f, 1.0f);
}

void PhysicsScheduler::SetSnapshotBuffer(std::shared_ptr<SnapshotBuffer> snapshotBuffer) {
    m_SnapshotBuffer = std::move(snapshotBuffer);
}

void PhysicsScheduler::FixedUpdate() {
    if (m_PhysicsWorld) {
        // Mutates live physics/game components for exactly one fixed tick.
        m_PhysicsWorld->Update(m_FixedTimeStep);

        ++m_PhysicsTick;
        m_SimulationTimeSeconds += m_FixedTimeStep;

        if (m_SnapshotBuffer) {
            // Publish a value-copy of the physics state for render to consume
            // without depending on live ECS component pointers.
            m_SnapshotBuffer->Publish(m_PhysicsWorld->CreateSnapshot(m_PhysicsTick, m_SimulationTimeSeconds));
        }
    }
}
