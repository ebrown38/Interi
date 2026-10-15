// SPDX-License-Identifier: MIT
// A full collapse hashed at fixed frames: identical at any worker count, and pinned across platforms.
#include "../ScenarioUtil.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

constexpr std::array<int, 3> kHashFrames{60, 120, 240};

/// The two-storey building losing its ground-floor columns, hashed at kHashFrames.
std::vector<std::uint64_t> CollapseHashes(std::uint32_t workers)
{
    Scenario scenario(workers);
    const std::vector<Cell> cells = TwoStoryBuildingCells();
    const DestructibleId id = scenario.Spawn(WithConcreteLimits(MakeGridAsset(cells, Concrete())));
    scenario.Step();
    for (const int x : {0, 3})
    {
        for (const int z : {0, 3})
        {
            const Vec3 column(static_cast<float>(x) + 0.5f, 1.5f, static_cast<float>(z) + 0.5f);
            const Vec3 offset(x == 0 ? 0.8f : -0.8f, 0.0f, z == 0 ? 0.8f : -0.8f);
            scenario.destruction.QueueDamage(
                {.center = column + offset, .energy = 1.0e6f, .innerRadius = 1.3f, .radius = 1.5f},
                DamageKind::Explosion);
        }
    }
    std::vector<std::uint64_t> hashes;
    for (int frame = 1; frame <= kHashFrames.back(); ++frame)
    {
        scenario.Step();
        if (std::ranges::find(kHashFrames, frame) != kHashFrames.end())
        {
            hashes.push_back(scenario.Hash(id));
        }
    }
    return hashes;
}

} // namespace

TEST(CollapseDeterminism, SameHashesAcrossThreadCounts)
{
    const std::vector<std::uint64_t> one = CollapseHashes(1);
    EXPECT_EQ(one, CollapseHashes(8));
    EXPECT_EQ(one, CollapseHashes(32));
}

TEST(CollapseDeterminism, MatchesCrossPlatformGoldenHash)
{
    // Pinned on Linux/Clang; MSVC must agree. A Jolt bump or any change to destruction output changes it.
    constexpr std::uint64_t kGolden = 0xdb38141747685403;
    const std::vector<std::uint64_t> hashes = CollapseHashes(4);
    EXPECT_EQ(hashes.back(), kGolden) << std::hex << "actual 0x" << hashes.back();
}
