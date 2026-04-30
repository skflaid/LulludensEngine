#pragma once

#include "Renderer/RenderCommandQueue.h"
#include "Renderer/RenderSnapshotBuilder.h"
#include <atomic>
#include <memory>

class SnapshotBuffer;

class RenderThreadContext {
public:
    RenderThreadContext(std::shared_ptr<SnapshotBuffer> snapshotBuffer, std::shared_ptr<RenderCommandQueue> commandQueue);

    // Standalone render-worker loop. The current GameEngine path inlines this
    // behavior in RunRenderThread().
    void Run(const std::atomic_bool& running);
    // Reads physics snapshots and produces the frame-local render snapshot.
    RenderSnapshot BuildSnapshot(float alpha);

private:
    // Physics-to-render handoff.
    std::shared_ptr<SnapshotBuffer> m_SnapshotBuffer;
    // Game/setup-to-render handoff.
    std::shared_ptr<RenderCommandQueue> m_CommandQueue;
    RenderSnapshotBuilder m_SnapshotBuilder;
};
