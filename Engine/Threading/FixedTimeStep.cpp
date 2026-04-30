#include "FixedTimeStep.h"

#include <algorithm>

FixedTimeStep::FixedTimeStep(double fixedDeltaSeconds)
    : m_FixedDeltaSeconds(fixedDeltaSeconds)
{
}

void FixedTimeStep::Accumulate(double deltaSeconds)
{
    // Clamp negative deltas so a clock hiccup cannot rewind the accumulator.
    m_AccumulatorSeconds += std::max(0.0, deltaSeconds);
}

bool FixedTimeStep::CanStep() const
{
    // The owner can run multiple fixed steps if the worker fell behind.
    return m_AccumulatorSeconds >= m_FixedDeltaSeconds;
}

void FixedTimeStep::ConsumeStep()
{
    // Keep any remainder; render interpolation uses this fractional time.
    m_AccumulatorSeconds = std::max(0.0, m_AccumulatorSeconds - m_FixedDeltaSeconds);
}

void FixedTimeStep::Reset()
{
    m_AccumulatorSeconds = 0.0;
}

float FixedTimeStep::GetAlpha() const
{
    // Fraction between the previous and current physics states.
    if (m_FixedDeltaSeconds <= 0.0) {
        return 0.0f;
    }

    const double alpha = m_AccumulatorSeconds / m_FixedDeltaSeconds;
    return static_cast<float>(std::clamp(alpha, 0.0, 1.0));
}
