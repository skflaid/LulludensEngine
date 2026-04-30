#pragma once
#include "PhysicsWorld.h"
#include <cstdint>
#include <memory>

class SnapshotBuffer;

class PhysicsScheduler {
public:
    PhysicsScheduler(std::shared_ptr<PhysicsWorld> world);

    // Accepts variable elapsed time from the caller and runs zero or more fixed
    // physics ticks. Returns how many fixed ticks were consumed this update.
    uint32_t Update(float deltaTime);

    void SetFixedTimeStep(float timeStep);
    float GetFixedTimeStep() const { return m_FixedTimeStep; }
    // Render uses this fraction to interpolate between the last two snapshots.
    float GetInterpolationAlpha() const;
    void SetSnapshotBuffer(std::shared_ptr<SnapshotBuffer> snapshotBuffer);

private:
    // One deterministic physics tick plus snapshot publication.
    void FixedUpdate();

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    // Cross-thread handoff from physics update to render snapshot building.
    std::shared_ptr<SnapshotBuffer> m_SnapshotBuffer;
    float m_FixedTimeStep = 1.0f / 60.0f;
    // Variable frame time accumulates here until enough exists for a fixed tick.
    float m_AccumulatedTime = 0.0f;
    uint64_t m_PhysicsTick = 0;
    double m_SimulationTimeSeconds = 0.0;
};
