#pragma once

#include "Physics/PhysicsSnapshot.h"

class TransformInterpolator {
public:
    // Render-side smoothing between two fixed physics snapshots.
    static PhysicsBodySnapshot Interpolate(
        const PhysicsBodySnapshot& previous,
        const PhysicsBodySnapshot& current,
        float alpha);
};
