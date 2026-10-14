// SPDX-License-Identifier: MIT
#include <interi/runtime/FixedStepClock.h>

#include <algorithm>

namespace interi
{

std::uint32_t FixedStepClock::Advance(std::chrono::nanoseconds elapsed) noexcept
{
    m_accumulator += std::max(elapsed, std::chrono::nanoseconds{0});
    const auto available = static_cast<std::uint64_t>(m_accumulator / m_config.step);
    const auto steps = static_cast<std::uint32_t>(std::min<std::uint64_t>(available, m_config.maxSubsteps));
    m_accumulator -= m_config.step * steps;
    if (m_accumulator >= m_config.step)
    {
        // Clamped: drop the whole steps that didn't fit, keep the fraction so Alpha stays meaningful.
        const std::chrono::nanoseconds remainder = m_accumulator % m_config.step;
        m_dropped += m_accumulator - remainder;
        m_accumulator = remainder;
    }
    m_totalSteps += steps;
    return steps;
}

} // namespace interi
