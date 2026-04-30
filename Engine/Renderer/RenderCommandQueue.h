#pragma once

#include "Renderer/RenderCommand.h"
#include <mutex>
#include <queue>
#include <vector>

class RenderCommandQueue {
public:
    // Producer API: game/setup code can enqueue render work safely.
    void Push(const RenderCommand& command);
    void Push(RenderCommand&& command);
    // Consumer API: render thread takes ownership of all pending commands.
    std::vector<RenderCommand> Drain();
    bool Empty() const;

private:
    // Protects the queue while producers and the render thread overlap.
    mutable std::mutex m_Mutex;
    std::queue<RenderCommand> m_Commands;
};
