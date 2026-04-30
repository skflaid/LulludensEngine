#pragma once

#include <functional>

class GameThreadContext {
public:
    using TickCallback = std::function<void(float)>;

    // Thin adapter for a future dedicated game thread. The current engine
    // drives this phase from GameEngine::Update instead.
    void SetTickCallback(TickCallback callback) { m_TickCallback = std::move(callback); }
    void Tick(float deltaSeconds);

private:
    TickCallback m_TickCallback;
};
