#include "RenderCommandQueue.h"

void RenderCommandQueue::Push(const RenderCommand& command)
{
    // Producer thread appends one command while the render thread may drain.
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Commands.push(command);
}

void RenderCommandQueue::Push(RenderCommand&& command)
{
    // Move-heavy version used when building commands locally.
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Commands.push(std::move(command));
}

std::vector<RenderCommand> RenderCommandQueue::Drain()
{
    std::vector<RenderCommand> drained;
    // Drain under a single lock so command order is preserved for this frame.
    std::lock_guard<std::mutex> lock(m_Mutex);

    while (!m_Commands.empty()) {
        drained.push_back(std::move(m_Commands.front()));
        m_Commands.pop();
    }

    return drained;
}

bool RenderCommandQueue::Empty() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Commands.empty();
}
