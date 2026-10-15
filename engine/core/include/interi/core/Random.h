// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace interi
{

/// Deterministic SplitMix64 generator (Steele, Lea, Flood 2014). Its output, and every value derived from it here,
/// is identical on all compilers and standard libraries: it uses only integer arithmetic and exact int-to-float
/// conversions, never <random> distributions (whose results are implementation-defined). Use it for anything that
/// must reproduce bit-exactly from a seed, such as fracture seed points.
class Random
{
public:
    explicit constexpr Random(std::uint64_t seed) noexcept
        : m_state(seed)
    {
    }

    /// Next raw 64-bit value.
    constexpr std::uint64_t NextU64() noexcept
    {
        std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// Uniform float in [0, 1): the top 24 bits, so every value is exactly representable.
    constexpr float NextFloat01() noexcept { return static_cast<float>(NextU64() >> 40) * (1.0f / 16777216.0f); }

    /// Uniform float in [min, max).
    constexpr float NextFloat(float min, float max) noexcept
    {
        const float value = min + (max - min) * NextFloat01();
        return value < max ? value : min; // rounding can reach max
    }

    /// Uniform integer in [0, bound). Requires bound > 0. Lemire's multiply-shift; the bias is below 2^-32.
    constexpr std::uint32_t NextBelow(std::uint32_t bound) noexcept
    {
        return static_cast<std::uint32_t>(((NextU64() >> 32) * bound) >> 32);
    }

private:
    std::uint64_t m_state;
};

} // namespace interi
