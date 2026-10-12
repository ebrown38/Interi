// SPDX-License-Identifier: MIT
#pragma once

#include <gtest/gtest.h>

#include <cstdint>

namespace interi::testing
{

/// Seed used by SeededTest when INTERI_TEST_SEED is not set.
inline constexpr std::uint64_t kDefaultTestSeed = 0x1A7E'5EED'0000'0001ull;

/// SplitMix64-based generator whose output is identical on every compiler and standard library
/// (unlike std:: distributions). Use it for all randomized test inputs.
class DeterministicRng
{
public:
    explicit DeterministicRng(std::uint64_t seed) noexcept
        : m_state(seed)
    {
    }

    /// Next raw 64-bit value.
    std::uint64_t NextU64() noexcept;

    /// Uniform float in [min, max).
    float NextFloat(float min, float max) noexcept;

    /// Uniform integer in [min, max] (both inclusive). Requires min <= max.
    std::int64_t NextInt(std::int64_t min, std::int64_t max) noexcept;

private:
    std::uint64_t m_state;
};

/// Parses an INTERI_TEST_SEED value: null/empty -> kDefaultTestSeed, decimal or 0x-prefixed hex.
/// Throws std::invalid_argument on anything else so a typo never silently changes the seed.
[[nodiscard]] std::uint64_t ResolveTestSeed(const char* envValue);

/// GoogleTest fixture that provides a DeterministicRng seeded from INTERI_TEST_SEED (or the default)
/// and records the seed in the test report so failures can be reproduced.
class SeededTest : public ::testing::Test
{
protected:
    SeededTest();

    [[nodiscard]] std::uint64_t Seed() const noexcept { return m_seed; }
    [[nodiscard]] DeterministicRng& Rng() noexcept { return m_rng; }

private:
    std::uint64_t m_seed;
    DeterministicRng m_rng;
};

} // namespace interi::testing
