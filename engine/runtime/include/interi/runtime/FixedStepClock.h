// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>

namespace interi
{

struct FixedStepConfig
{
    /// Simulation step. The default is 60 Hz, rounded to whole nanoseconds.
    std::chrono::nanoseconds step{16'666'667};
    /// Most steps one frame may run. A longer frame (a hitch, a debugger break) drops the extra whole steps rather
    /// than spiralling: each step would take longer than the time it simulates.
    std::uint32_t maxSubsteps = 8;
};

/// Converts variable frame times into a whole number of fixed simulation steps plus an interpolation fraction.
/// Time is fed in by the caller (no clock reads), so it is deterministic and testable. Integer nanoseconds keep
/// the accumulator exact.
class FixedStepClock
{
public:
    explicit FixedStepClock(FixedStepConfig config = {}) noexcept
        : m_config(config)
    {
    }

    /// Adds a frame's elapsed time and returns how many steps to simulate now (at most maxSubsteps).
    std::uint32_t Advance(std::chrono::nanoseconds elapsed) noexcept;

    /// Fraction of a step left in the accumulator, in [0, 1): how far rendering is between the last two
    /// simulated states.
    [[nodiscard]] double Alpha() const noexcept
    {
        return static_cast<double>(m_accumulator.count()) / static_cast<double>(m_config.step.count());
    }

    [[nodiscard]] std::chrono::nanoseconds Step() const noexcept { return m_config.step; }
    [[nodiscard]] double StepSeconds() const noexcept { return std::chrono::duration<double>(m_config.step).count(); }
    [[nodiscard]] std::uint64_t TotalSteps() const noexcept { return m_totalSteps; }
    /// Time discarded by the substep clamp since construction.
    [[nodiscard]] std::chrono::nanoseconds DroppedTime() const noexcept { return m_dropped; }

private:
    FixedStepConfig m_config;
    std::chrono::nanoseconds m_accumulator{0};
    std::chrono::nanoseconds m_dropped{0};
    std::uint64_t m_totalSteps = 0;
};

} // namespace interi
