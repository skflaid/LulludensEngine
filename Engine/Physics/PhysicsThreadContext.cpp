#include "PhysicsThreadContext.h"

#include "Physics/PhysicsWorld.h"
#include "Threading/SnapshotBuffer.h"
#include <chrono>
#include <thread>

PhysicsThreadContext::PhysicsThreadContext(
    std::shared_ptr<PhysicsWorld> world,
    std::shared_ptr<SnapshotBuffer> snapshotBuffer)
    : m_PhysicsWorld(std::move(world)),
      m_SnapshotBuffer(std::move(snapshotBuffer)),
      m_FixedTimeStep(1.0 / 60.0)
{
}

void PhysicsThreadContext::Run(const std::atomic_bool& running)
{
    auto previousTime = std::chrono::steady_clock::now();

    while (running.load()) {
        // Worker-local clock: collect variable real time, then consume it as
        // fixed simulation steps below.
        const auto currentTime = std::chrono::steady_clock::now();
        const std::chrono::duration<double> delta = currentTime - previousTime;
        previousTime = currentTime;

        m_FixedTimeStep.Accumulate(delta.count());
        while (m_FixedTimeStep.CanStep()) {
            // A slow frame may require several fixed physics ticks to catch up.
            Step(static_cast<float>(m_FixedTimeStep.GetFixedDeltaSeconds()));
            m_FixedTimeStep.ConsumeStep();
        }

        // Cooperative yield keeps this loop responsive without burning a core.
        std::this_thread::yield();
    }
}

void PhysicsThreadContext::Step(float deltaSeconds)
{
    if (!m_PhysicsWorld) {
        return;
    }

    // Break one fixed tick into substeps when the configured maximum step size
    // says the physics integration would otherwise be too coarse.
    for (float substep : m_SubstepController.BuildSubsteps(deltaSeconds)) {
        m_PhysicsWorld->Update(substep);
    }

    ++m_TickIndex;
    m_SimulationTimeSeconds += deltaSeconds;

    if (m_SnapshotBuffer) {
        // Last action of a completed tick: publish the state render should see.
        m_SnapshotBuffer->Publish(m_PhysicsWorld->CreateSnapshot(m_TickIndex, m_SimulationTimeSeconds));
    }
}
