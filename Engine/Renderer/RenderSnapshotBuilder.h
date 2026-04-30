#pragma once

#include "Renderer/RenderSnapshot.h"
#include "Threading/SnapshotBuffer.h"
#include <optional>

class RenderSnapshotBuilder {
public:
    // Converts physics snapshots plus game-thread camera state into the render
    // snapshot consumed by draw passes.
    RenderSnapshot Build(
        const PhysicsSnapshotPair& snapshots,
        float alpha,
        const std::optional<CameraLogicState>& cameraState = std::nullopt) const;

private:
    CameraRenderState BuildCamera(const CameraLogicState& cameraState) const;
};
