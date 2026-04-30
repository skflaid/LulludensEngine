#pragma once

#include "Physics/PhysicsSnapshot.h"
#include <mutex>
#include <optional>

struct PhysicsSnapshotPair {
    std::optional<PhysicsSnapshot> Previous;
    std::optional<PhysicsSnapshot> Current;

    // Render interpolation needs both snapshots. The current one alone is
    // enough for a non-interpolated fallback.
    bool HasBoth() const { return Previous.has_value() && Current.has_value(); }
    bool HasCurrent() const { return Current.has_value(); }
};

class SnapshotBuffer {
public:
    // Producer side: physics publishes one immutable snapshot per fixed tick.
    void Publish(PhysicsSnapshot snapshot);
    // Consumer side: render reads previous/current together under the mutex.
    PhysicsSnapshotPair AcquirePair() const;
    std::optional<PhysicsSnapshot> AcquireLatest() const;
    void Clear();

private:
    // Protects the two-slot snapshot history during cross-thread handoff.
    mutable std::mutex m_Mutex;
    std::optional<PhysicsSnapshot> m_Previous;
    std::optional<PhysicsSnapshot> m_Current;
};
