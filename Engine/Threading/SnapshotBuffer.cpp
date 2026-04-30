#include "SnapshotBuffer.h"

void SnapshotBuffer::Publish(PhysicsSnapshot snapshot)
{
    // Physics thread advances the two-snapshot history atomically for readers.
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Previous = std::move(m_Current);
    m_Current = std::move(snapshot);
}

PhysicsSnapshotPair SnapshotBuffer::AcquirePair() const
{
    // Render thread copies both snapshots while the producer is blocked, so it
    // never observes a mismatched previous/current pair.
    std::lock_guard<std::mutex> lock(m_Mutex);
    return { m_Previous, m_Current };
}

std::optional<PhysicsSnapshot> SnapshotBuffer::AcquireLatest() const
{
    // Useful when a consumer does not need interpolation history.
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Current;
}

void SnapshotBuffer::Clear()
{
    // Called after workers are stopped so no consumer keeps stale frame data.
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Previous.reset();
    m_Current.reset();
}
