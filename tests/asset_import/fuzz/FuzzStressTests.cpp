// SPDX-License-Identifier: MIT
#include <interi/testing/DeterministicRng.h>

#include "FuzzTargets.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace interi;
using namespace interi::fuzz;

namespace
{

constexpr std::size_t kMaxInput = std::size_t{1} << 16;

const char* Env(const char* name)
{
#if defined(_MSC_VER)
#pragma warning(suppress : 4996) // getenv is fine here: read-only, single-threaded test setup
    return std::getenv(name);
#else
    return std::getenv(name);
#endif
}

/// Number of mutations per seed: INTERI_FUZZ_ITERATIONS, default 300.
std::size_t Iterations()
{
    if (const char* text = Env("INTERI_FUZZ_ITERATIONS"))
    {
        return static_cast<std::size_t>(std::strtoull(text, nullptr, 10));
    }
    return 300;
}

/// Bit flips, byte writes, deletions, duplications, insertions, truncation, interesting integers and, for text, a
/// number swapped for an extreme one.
void Mutate(std::vector<std::byte>& data, interi::testing::DeterministicRng& rng)
{
    const auto pick = [&](std::size_t size)
    { return static_cast<std::size_t>(rng.NextInt(0, static_cast<std::int64_t>(size) - 1)); };
    const std::size_t rounds = static_cast<std::size_t>(rng.NextInt(1, 6));
    for (std::size_t round = 0; round < rounds && !data.empty(); ++round)
    {
        switch (rng.NextInt(0, 8))
        {
        case 0:
            data[pick(data.size())] ^= static_cast<std::byte>(1u << rng.NextInt(0, 7));
            break;
        case 1:
            data[pick(data.size())] = static_cast<std::byte>(rng.NextInt(0, 255));
            break;
        case 2: // delete a range
        {
            const std::size_t from = pick(data.size());
            const std::size_t count =
                std::min<std::size_t>(data.size() - from, static_cast<std::size_t>(rng.NextInt(1, 16)));
            data.erase(data.begin() + static_cast<std::ptrdiff_t>(from),
                       data.begin() + static_cast<std::ptrdiff_t>(from + count));
            break;
        }
        case 3: // duplicate a range
        {
            const std::size_t from = pick(data.size());
            const std::size_t count =
                std::min<std::size_t>(data.size() - from, static_cast<std::size_t>(rng.NextInt(1, 32)));
            const std::vector<std::byte> copy(data.begin() + static_cast<std::ptrdiff_t>(from),
                                              data.begin() + static_cast<std::ptrdiff_t>(from + count));
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(pick(data.size())), copy.begin(), copy.end());
            break;
        }
        case 4: // insert random bytes
        {
            const std::size_t count = static_cast<std::size_t>(rng.NextInt(1, 8));
            std::vector<std::byte> noise(count);
            for (std::byte& b : noise)
            {
                b = static_cast<std::byte>(rng.NextInt(0, 255));
            }
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(pick(data.size())), noise.begin(), noise.end());
            break;
        }
        case 5: // truncate
            data.resize(pick(data.size()) + 1);
            break;
        case 6: // an interesting 32-bit value
        {
            static constexpr std::uint32_t kValues[] = {
                0u, 1u, 3u, 255u, 65535u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x3F800000u, 0x7FC00000u};
            const std::uint32_t value = kValues[rng.NextInt(0, 9)];
            const std::size_t at = pick(data.size());
            for (std::size_t i = 0; i < 4 && at + i < data.size(); ++i)
            {
                data[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
            }
            break;
        }
        case 7: // swap a decimal number for an extreme one (text formats)
        {
            std::size_t at = pick(data.size());
            for (std::size_t tries = 0; tries < data.size(); ++tries, at = (at + 1) % data.size())
            {
                const auto c = static_cast<char>(data[at]);
                if (c >= '0' && c <= '9')
                {
                    break;
                }
            }
            std::size_t end = at;
            while (end < data.size() && ((static_cast<char>(data[end]) >= '0' && static_cast<char>(data[end]) <= '9') ||
                                         static_cast<char>(data[end]) == '.'))
            {
                ++end;
            }
            static constexpr const char* kNumbers[] = {"4294967295", "-1",   "1e38",       "99999999999",
                                                       "0",          "-0.0", "2147483648", "65536"};
            const std::string text = kNumbers[rng.NextInt(0, 7)];
            std::vector<std::byte> replacement(text.size());
            std::memcpy(replacement.data(), text.data(), text.size());
            data.erase(data.begin() + static_cast<std::ptrdiff_t>(at), data.begin() + static_cast<std::ptrdiff_t>(end));
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(at), replacement.begin(), replacement.end());
            break;
        }
        default: // splice: the start of the file repeated at a random place
        {
            const std::size_t count = std::min<std::size_t>(data.size(), static_cast<std::size_t>(rng.NextInt(1, 24)));
            const std::vector<std::byte> head(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(count));
            data.insert(data.begin() + static_cast<std::ptrdiff_t>(pick(data.size())), head.begin(), head.end());
            break;
        }
        }
    }
    if (data.size() > kMaxInput)
    {
        data.resize(kMaxInput);
    }
}

} // namespace

class ImportFuzz : public interi::testing::SeededTest
{
};

TEST_F(ImportFuzz, EveryTarget_SurvivesMutatedSeeds)
{
    const std::vector<fuzz::Seed> seeds = SeedCorpus();
    ASSERT_FALSE(seeds.empty());
    std::size_t runs = 0;
    for (const fuzz::Seed& seed : seeds)
    {
        // The unmodified seed must pass its own checks and, being valid, parse.
        EXPECT_EQ(fuzz::Run(seed.target, seed.data), "") << Name(seed.target) << " " << seed.name;
        for (std::size_t i = 0; i < Iterations(); ++i)
        {
            std::vector<std::byte> data = seed.data;
            Mutate(data, Rng());
            const std::string problem = fuzz::Run(seed.target, data);
            ++runs;
            ASSERT_EQ(problem, "") << Name(seed.target) << " " << seed.name << " mutation " << i << " (seed " << Seed()
                                   << ")";
        }
    }
    EXPECT_GT(runs, 0u);
}

TEST_F(ImportFuzz, EveryTarget_SurvivesRandomBytesAndEmptyInput)
{
    for (std::size_t t = 0; t < static_cast<std::size_t>(Target::Count); ++t)
    {
        const auto target = static_cast<Target>(t);
        EXPECT_EQ(fuzz::Run(target, {}), "") << Name(target);
        for (std::size_t i = 0; i < Iterations() / 4; ++i)
        {
            std::vector<std::byte> data(static_cast<std::size_t>(Rng().NextInt(1, 512)));
            for (std::byte& b : data)
            {
                b = static_cast<std::byte>(Rng().NextInt(0, 255));
            }
            ASSERT_EQ(fuzz::Run(target, data), "") << Name(target) << " (seed " << Seed() << ")";
        }
    }
}

TEST_F(ImportFuzz, SeedsAreValidInputs)
{
    // Every seed is accepted by the parser it is meant for (a seed that fails would fuzz nothing).
    std::size_t accepted = 0;
    for (const fuzz::Seed& seed : SeedCorpus())
    {
        EXPECT_EQ(fuzz::Run(seed.target, seed.data), "") << seed.name;
        ++accepted;
    }
    EXPECT_GE(accepted, 20u);
}

/// With INTERI_WRITE_FUZZ_CORPUS=<dir>: writes the seeds there, each file prefixed with its target byte (the format
/// the libFuzzer harness expects). Not a test of anything: a way to refresh tests/asset_import/fuzz/corpus.
TEST(ImportFuzzCorpus, WriteWhenAsked)
{
    const char* directory = Env("INTERI_WRITE_FUZZ_CORPUS");
    if (directory == nullptr || *directory == '\0')
    {
        GTEST_SKIP() << "INTERI_WRITE_FUZZ_CORPUS is not set";
    }
    std::filesystem::create_directories(directory);
    for (const fuzz::Seed& seed : SeedCorpus())
    {
        std::ofstream out(std::filesystem::path(directory) / (std::string(Name(seed.target)) + "-" + seed.name),
                          std::ios::binary);
        const char selector = static_cast<char>(seed.target);
        out.write(&selector, 1);
        out.write(reinterpret_cast<const char*>(seed.data.data()), static_cast<std::streamsize>(seed.data.size()));
    }
}
