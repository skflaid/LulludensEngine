#include "GameThreadContext.h"

void GameThreadContext::Tick(float deltaSeconds)
{
    // Called by whichever loop owns the game phase; delegates to engine logic.
    if (m_TickCallback) {
        m_TickCallback(deltaSeconds);
    }
}
