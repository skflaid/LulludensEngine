#pragma once

class FixedTimeStep {
public:
    explicit FixedTimeStep(double fixedDeltaSeconds = 1.0 / 60.0);

    // Add variable wall-clock time from a worker loop.
    void Accumulate(double deltaSeconds);
    // True while enough accumulated time exists for another fixed simulation.
    bool CanStep() const;
    // Remove exactly one fixed step after physics has advanced.
    void ConsumeStep();
    void Reset();

    double GetFixedDeltaSeconds() const { return m_FixedDeltaSeconds; }
    double GetAccumulatorSeconds() const { return m_AccumulatorSeconds; }
    float GetAlpha() const;

private:
    // Target simulation step, independent of render frame rate.
    double m_FixedDeltaSeconds = 1.0 / 60.0;
    // Leftover wall-clock time waiting to become one or more fixed steps.
    double m_AccumulatorSeconds = 0.0;
};
