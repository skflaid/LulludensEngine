#include "RenderThreadContext.h"

#include "Threading/SnapshotBuffer.h"
#include <chrono>
#include <thread>

RenderThreadContext::RenderThreadContext(
    std::shared_ptr<SnapshotBuffer> snapshotBuffer,
    std::shared_ptr<RenderCommandQueue> commandQueue)
    : m_SnapshotBuffer(std::move(snapshotBuffer)), m_CommandQueue(std::move(commandQueue))
{
}

void RenderThreadContext::Run(const std::atomic_bool& running)
{
    while (running.load()) {
        if (m_CommandQueue) {
            // Consume render commands enqueued by non-render phases.
            (void)m_CommandQueue->Drain();
        }

        // Build the latest snapshot view for this render iteration.
        (void)BuildSnapshot(0.0f);
        std::this_thread::yield();
    }
}

RenderSnapshot RenderThreadContext::BuildSnapshot(float alpha)
{
    if (!m_SnapshotBuffer) {
        return RenderSnapshot();
    }

    // SnapshotBuffer returns copied previous/current physics states under lock.
    return m_SnapshotBuilder.Build(m_SnapshotBuffer->AcquirePair(), alpha);
}
