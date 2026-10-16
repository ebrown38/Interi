// SPDX-License-Identifier: MIT
#include <interi/core/Random.h>
#include <interi/destruction_audio/ImpactBatcher.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using interi::Random;
using interi::Vec3;
using interi::destruction::ImpactBatcher;
using interi::destruction::ImpactGroup;
using interi::destruction::ImpactSound;

TEST(ImpactBatcher, ManyContactsSameRegion_OneVoice)
{
    ImpactBatcher batcher(4.0f);
    Random random(3);
    float total = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
        const float impulse = random.NextFloat(100.0f, 1000.0f);
        total += impulse;
        batcher.Add({.point = Vec3(random.NextFloat(-1.9f, 1.9f), random.NextFloat(-1.9f, 1.9f), 1.0f),
                     .impulse = impulse,
                     .bank = 2});
    }
    const auto groups = batcher.Flush();
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].count, 300u);
    EXPECT_EQ(groups[0].bank, 2u);
    EXPECT_NEAR(groups[0].impulse, total, 1e-2f * total / 300.0f);
    EXPECT_GT(groups[0].position.x, -1.9f);
    EXPECT_LT(groups[0].position.x, 1.9f);
    EXPECT_TRUE(batcher.Flush().empty()); // emptied
}

TEST(ImpactBatcher, DifferentRegionsAndBanks_SeparateVoices)
{
    ImpactBatcher batcher(4.0f);
    // Cells of 4 m centred on multiples of 4: x in [-2, 2) is cell 0.
    batcher.Add({.point = Vec3(-0.5f, 0.0f, 0.0f), .impulse = 100.0f, .bank = 0});
    batcher.Add({.point = Vec3(9.0f, 0.0f, 0.0f), .impulse = 300.0f, .bank = 0}); // another cell
    batcher.Add({.point = Vec3(1.5f, 0.0f, 0.0f), .impulse = 100.0f, .bank = 1}); // same cell, other bank
    batcher.Add({.point = Vec3(-3.0f, 0.0f, 0.0f), .impulse = 50.0f, .bank = 0}); // negative cell
    batcher.Add({.point = Vec3(1.5f, 0.0f, 0.0f), .impulse = 300.0f, .bank = 0});
    const auto groups = batcher.Flush();
    ASSERT_EQ(groups.size(), 4u);
    // Sorted by bank, then cell.
    EXPECT_EQ(groups[0].bank, 0u);
    EXPECT_EQ(groups[0].count, 1u); // x = -3
    EXPECT_EQ(groups[1].count, 2u); // x = -0.5 and 1.5: impulse-weighted centroid
    EXPECT_NEAR(groups[1].position.x, (-0.5f * 100.0f + 1.5f * 300.0f) / 400.0f, 1e-5f);
    EXPECT_EQ(groups[1].peakImpulse, 300.0f);
    EXPECT_EQ(groups[2].count, 1u); // x = 9
    EXPECT_EQ(groups[3].bank, 1u);
}

TEST(ImpactBatcher, AddOrder_DoesNotChangeResult)
{
    std::vector<ImpactSound> sounds;
    Random random(11);
    for (int i = 0; i < 200; ++i)
    {
        sounds.push_back({.point = Vec3(random.NextFloat(-20.0f, 20.0f), 0.0f, random.NextFloat(-20.0f, 20.0f)),
                          .impulse = random.NextFloat(1.0f, 5000.0f),
                          .bank = random.NextBelow(3)});
    }
    const auto run = [](const std::vector<ImpactSound>& input)
    {
        ImpactBatcher batcher;
        for (const ImpactSound& sound : input)
        {
            batcher.Add(sound);
        }
        const auto groups = batcher.Flush();
        return std::vector<ImpactGroup>(groups.begin(), groups.end());
    };
    const std::vector<ImpactGroup> forward = run(sounds);
    std::ranges::reverse(sounds);
    const std::vector<ImpactGroup> backward = run(sounds);
    ASSERT_EQ(forward.size(), backward.size());
    for (std::size_t i = 0; i < forward.size(); ++i)
    {
        EXPECT_EQ(forward[i].impulse, backward[i].impulse);
        EXPECT_EQ(forward[i].position.x, backward[i].position.x);
        EXPECT_EQ(forward[i].count, backward[i].count);
    }
}
