// SPDX-License-Identifier: MIT
#include <interi/destruction_audio/StructureAudio.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "../gameplay/GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <memory>
#include <vector>

using interi::Transform;
using interi::Vec3;
using interi::destruction::BondUtilization;
using interi::destruction::CollapseAudio;
using interi::destruction::CollapseEvent;
using interi::destruction::CreakEvent;
using interi::destruction::DamageKind;
using interi::destruction::DestructibleAsset;
using interi::destruction::DestructibleId;
using interi::destruction::DestructionWorld;
using interi::destruction::StressAudio;
using interi::destruction::testing::BoxCells;
using interi::destruction::testing::FindBond;
using interi::destruction::testing::GridAssetOptions;
using interi::destruction::testing::MakeGridAsset;
using interi::testing::GameplayScene;
using interi::testing::kGameplayStep;

namespace
{

constexpr float kCubeMass = 1000.0f;
constexpr float kGravity = 9.81f;

/// A column of three unit cubes on an anchored base cube; the bottom bond carries two cubes' weight.
class StructureAudioTest : public ::testing::Test
{
protected:
    DestructibleId SpawnColumn(GridAssetOptions options)
    {
        options.mass = kCubeMass;
        m_asset = std::make_shared<const DestructibleAsset>(MakeGridAsset(BoxCells(1, 3, 1), options));
        return destruction.Spawn(scene.world, scene.jobs, m_asset, Transform{});
    }

    void Step(int count = 1)
    {
        for (int i = 0; i < count; ++i)
        {
            scene.Step();
            destruction.Update(scene.world, scene.jobs);
        }
    }

    [[nodiscard]] std::uint32_t BottomBond() const { return *FindBond(*m_asset, 0, 1); }
    [[nodiscard]] std::uint32_t TopBond() const { return *FindBond(*m_asset, 1, 2); }

    GameplayScene scene;
    DestructionWorld destruction{scene.physics};

private:
    std::shared_ptr<const DestructibleAsset> m_asset;
};

GridAssetOptions Limit(float limit)
{
    GridAssetOptions options;
    options.limit = limit;
    return options;
}

} // namespace

TEST_F(StructureAudioTest, BondUtilization_LoadOverLimit)
{
    const float bottomLoad = 2.0f * kCubeMass * kGravity; // N of compression
    const DestructibleId column = SpawnColumn(Limit(bottomLoad / 0.9f));
    Step(3);
    const auto& solver = destruction.Stress(column);
    ASSERT_TRUE(solver.IsConverged());
    EXPECT_NEAR(BondUtilization(solver, BottomBond(), 1.0f), 0.9f, 0.02f);
    EXPECT_NEAR(BondUtilization(solver, TopBond(), 1.0f), 0.45f, 0.02f);
    EXPECT_NEAR(BondUtilization(solver, BottomBond(), 0.5f), 1.8f, 0.04f); // damaged: limits scale with health
}

TEST_F(StructureAudioTest, StressAudio_NearLimit_TriggersCreak)
{
    const DestructibleId column = SpawnColumn(Limit(2.0f * kCubeMass * kGravity / 0.9f));
    Step(3);
    StressAudio stress;
    std::vector<CreakEvent> creaks;
    stress.Update(destruction, kGameplayStep, creaks);
    ASSERT_EQ(creaks.size(), 1u); // the bottom bond only (the top one is at 0.45)
    EXPECT_EQ(creaks[0].instance, column);
    EXPECT_NEAR(creaks[0].position.y, 1.0f, 1e-4f);
    EXPECT_NEAR(creaks[0].utilization, 0.9f, 0.02f);

    // Sustained strain creaks once, not every frame.
    for (int i = 0; i < 300; ++i)
    {
        stress.Update(destruction, kGameplayStep, creaks);
    }
    EXPECT_EQ(creaks.size(), 1u);
}

TEST_F(StructureAudioTest, StressAudio_BelowThreshold_Silent)
{
    SpawnColumn(Limit(1.0e9f));
    Step(3);
    StressAudio stress;
    std::vector<CreakEvent> creaks;
    for (int i = 0; i < 10; ++i)
    {
        stress.Update(destruction, kGameplayStep, creaks);
    }
    EXPECT_TRUE(creaks.empty());
}

TEST_F(StructureAudioTest, CollapseRumble_FallingMassReported)
{
    GridAssetOptions fragile;
    fragile.fractureEnergy = 100.0f;
    SpawnColumn(fragile);
    Step();
    CollapseAudio collapse;
    std::vector<CollapseEvent> events;
    collapse.Update(scene.world, destruction, events);
    EXPECT_TRUE(events.empty()); // standing structures don't rumble
    Step(10);
    collapse.Update(scene.world, destruction, events);
    EXPECT_TRUE(events.empty());

    // Break the bottom bond: the two cubes above fall as one piece.
    destruction.QueueDamage({.center = Vec3(0.5f, 1.0f, 0.5f), .energy = 1000.0f, .innerRadius = 0.3f, .radius = 0.6f},
                            DamageKind::Explosion);
    Step();
    collapse.Update(scene.world, destruction, events);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_NEAR(events[0].mass, 2.0f * kCubeMass, 1e-2f);
    EXPECT_NEAR(events[0].position.y, 2.0f, 0.1f);

    Step();
    collapse.Update(scene.world, destruction, events);
    EXPECT_EQ(events.size(), 1u); // counted once
}
