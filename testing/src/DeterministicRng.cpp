// SPDX-License-Identifier: MIT
#include <interi/testing/DeterministicRng.h>

#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace interi::testing
{

std::uint64_t DeterministicRng::NextU64() noexcept
{
    // SplitMix64 (Steele, Lea, Flood 2014), public-domain reference algorithm.
    std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

float DeterministicRng::NextFloat(float min, float max) noexcept
{
    // Top 24 bits -> exactly representable float in [0, 1).
    const float unit = static_cast<float>(NextU64() >> 40) * (1.0f / 16777216.0f);
    const float value = min + (max - min) * unit;
    return value < max ? value : min; // guard against rounding up to max
}

std::int64_t DeterministicRng::NextInt(std::int64_t min, std::int64_t max) noexcept
{
    const std::uint64_t span = static_cast<std::uint64_t>(max) - static_cast<std::uint64_t>(min) + 1u;
    if (span == 0) // full 64-bit range
    {
        return static_cast<std::int64_t>(NextU64());
    }
    // Rejection sampling removes modulo bias.
    const std::uint64_t limit = UINT64_MAX - (UINT64_MAX % span);
    std::uint64_t value = 0;
    do
    {
        value = NextU64();
    } while (value >= limit);
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(min) + value % span);
}

std::uint64_t ResolveTestSeed(const char* envValue)
{
    if (envValue == nullptr || *envValue == '\0')
    {
        return kDefaultTestSeed;
    }
    std::string_view text(envValue);
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X"))
    {
        text.remove_prefix(2);
        base = 16;
    }
    std::uint64_t seed = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), seed, base);
    if (ec != std::errc{} || end != text.data() + text.size() || text.empty())
    {
        throw std::invalid_argument("INTERI_TEST_SEED is not a valid decimal or 0x-hex integer: " +
                                    std::string(envValue));
    }
    return seed;
}

namespace
{
const char* ReadSeedEnv()
{
#if defined(_MSC_VER)
#pragma warning(suppress : 4996) // getenv is fine here: read-only, single-threaded test setup
    return std::getenv("INTERI_TEST_SEED");
#else
    return std::getenv("INTERI_TEST_SEED");
#endif
}
} // namespace

SeededTest::SeededTest()
    : m_seed(ResolveTestSeed(ReadSeedEnv()))
    , m_rng(m_seed)
{
    RecordProperty("seed", std::to_string(m_seed));
}

} // namespace interi::testing
