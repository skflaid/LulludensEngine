#pragma once

#include <vector>

class PhysicsSubstepController {
public:
    PhysicsSubstepController(unsigned int maxSubsteps = 4, float maxSubstepSeconds = 1.0f / 120.0f);

    // Converts one fixed tick into smaller integration slices when needed.
    std::vector<float> BuildSubsteps(float deltaSeconds) const;

    void SetMaxSubsteps(unsigned int maxSubsteps) { m_MaxSubsteps = maxSubsteps; }
    void SetMaxSubstepSeconds(float maxSubstepSeconds) { m_MaxSubstepSeconds = maxSubstepSeconds; }

    unsigned int GetMaxSubsteps() const { return m_MaxSubsteps; }
    float GetMaxSubstepSeconds() const { return m_MaxSubstepSeconds; }

private:
    unsigned int m_MaxSubsteps = 4;
    float m_MaxSubstepSeconds = 1.0f / 120.0f;
};
