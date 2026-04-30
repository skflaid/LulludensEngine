#pragma once

#include "Physics/PhysicsSubstepController.h"
#include "Threading/FixedTimeStep.h"
#include <atomic>
#include <memory>

class PhysicsWorld;
class SnapshotBuffer;

class PhysicsThreadContext {
public:
    PhysicsThreadContext(std::shared_ptr<PhysicsWorld> world, std::shared_ptr<SnapshotBuffer> snapshotBuffer);

    // Standalone physics-worker loop. The caller owns the running flag.
    void Run(const std::atomic_bool& running);
    // Advances one fixed physics step, optionally split into smaller substeps.
    void Step(float deltaSeconds);

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    // Publishes completed physics ticks for render consumers.
    std::shared_ptr<SnapshotBuffer> m_SnapshotBuffer;
    FixedTimeStep m_FixedTimeStep;
    // Keeps large deltas from becoming one unstable physics update.
    PhysicsSubstepController m_SubstepController;
    uint64_t m_TickIndex = 0;
    double m_SimulationTimeSeconds = 0.0;
};
