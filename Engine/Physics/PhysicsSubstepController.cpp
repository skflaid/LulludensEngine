#include "PhysicsSubstepController.h"

#include <algorithm>
#include <cmath>

PhysicsSubstepController::PhysicsSubstepController(unsigned int maxSubsteps, float maxSubstepSeconds)
    : m_MaxSubsteps(maxSubsteps), m_MaxSubstepSeconds(maxSubstepSeconds)
{
}

std::vector<float> PhysicsSubstepController::BuildSubsteps(float deltaSeconds) const
{
    std::vector<float> substeps;
    if (deltaSeconds <= 0.0f || m_MaxSubsteps == 0 || m_MaxSubstepSeconds <= 0.0f) {
        return substeps;
    }

    // Choose enough slices to keep each integration below the maximum, capped
    // to prevent a spiral of too many catch-up steps.
    const unsigned int desiredSteps =
        static_cast<unsigned int>(std::ceil(deltaSeconds / m_MaxSubstepSeconds));
    const unsigned int stepCount = std::max(1u, std::min(m_MaxSubsteps, desiredSteps));
    const float substepSeconds = deltaSeconds / static_cast<float>(stepCount);

    substeps.assign(stepCount, substepSeconds);
    return substeps;
}
