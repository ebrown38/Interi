// SPDX-License-Identifier: MIT
// Whole-structure collapses on Jolt (label `integration`).
#include "ScenarioUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

constexpr int kFiveSeconds = 300;

} // namespace

TEST(Scenario, TwoStoryBuilding_StandsOnItsOwn)
{
    Scenario scenario(4);
    const std::vector<Cell> cells = TwoStoryBuildingCells();
    const DestructibleId id = scenario.Spawn(WithConcreteLimits(MakeGridAsset(cells, Concrete())));
    for (int frame = 0; frame < 60; ++frame)
    {
        scenario.Step();
        ASSERT_EQ(scenario.destruction.LastStats().bondsBrokenByStress, 0u) << "frame " << frame;
    }
    for (std::uint32_t chunk = 0; chunk < cells.size(); ++chunk)
    {
        EXPECT_TRUE(scenario.ChunkAnchored(id, chunk));
    }
}

TEST(Scenario, TwoStoryBuilding_DestroyGroundColumns_CollapsesWithin5s)
{
    Scenario scenario(4);
    const std::vector<Cell> cells = TwoStoryBuildingCells();
    const DestructibleId id = scenario.Spawn(WithConcreteLimits(MakeGridAsset(cells, Concrete())));
    scenario.Step();

    // Blow out the ground-floor columns: each charge sits beside a column's upper chunk (y = 1) towards the inside,
    // breaking both of its bonds and throwing it clear.
    for (const int x : {0, 3})
    {
        for (const int z : {0, 3})
        {
            const float inward = 0.8f;
            const Vec3 column(static_cast<float>(x) + 0.5f, 1.5f, static_cast<float>(z) + 0.5f);
            const Vec3 offset(x == 0 ? inward : -inward, 0.0f, z == 0 ? inward : -inward);
            scenario.destruction.QueueDamage(
                {.center = column + offset, .energy = 1.0e6f, .innerRadius = 1.3f, .radius = 1.5f},
                DamageKind::Explosion);
        }
    }

    const std::uint32_t roof = CellIndex(cells, {1, 5, 1});
    const float roofStart = scenario.ChunkPosition(id, roof).y;
    int collapsedFrame = -1;
    for (int frame = 0; frame < kFiveSeconds && collapsedFrame < 0; ++frame)
    {
        scenario.Step();
        float anchoredTop = 0.0f;
        for (std::uint32_t chunk = 0; chunk < cells.size(); ++chunk)
        {
            if (scenario.ChunkAnchored(id, chunk))
            {
                anchoredTop = std::max(anchoredTop, scenario.ChunkPosition(id, chunk).y + 0.5f);
            }
        }
        if (anchoredTop <= 1.0f + 1e-4f && scenario.ChunkPosition(id, roof).y < roofStart - 1.0f)
        {
            collapsedFrame = frame;
        }
    }
    EXPECT_GE(collapsedFrame, 0) << "anchored chunks above the ground floor, or the roof did not come down";
}

TEST(Scenario, TowerTopple_NoBodiesExplode)
{
    DestructionSettings settings;
    settings.explosionPushSpeed = 0.0f;
    Scenario scenario(4, settings);
    const std::vector<Cell> cells = BoxCells(2, 8, 2);
    const DestructibleAsset asset = WithConcreteLimits(MakeGridAsset(cells, Concrete()));
    const DestructibleId id = scenario.Spawn(asset);
    scenario.Step();

    // Cut the tower off its base (the bonds of the y = 1 plane only), then tip it over.
    for (std::uint32_t bond = 0; bond < asset.bonds.size(); ++bond)
    {
        if (asset.bonds[bond].normal.y > 0.5f && std::abs(asset.bonds[bond].centroid.y - 1.0f) < 1e-4f)
        {
            scenario.destruction.QueueDamage(
                {.center = asset.bonds[bond].centroid, .energy = 1.0e9f, .innerRadius = 0.05f, .radius = 0.05f},
                DamageKind::Explosion);
        }
    }
    scenario.destruction.Update(scenario.world, scenario.jobs);
    ASSERT_EQ(scenario.destruction.LastStats().bondsBrokenByDamage, 4u);
    scenario.sync.Step(scenario.world, scenario.jobs, kScenarioStep); // creates the tower's body
    const Entity tower = scenario.destruction.PieceEntity(id, scenario.destruction.PieceCount(id) - 1);
    const JPH::BodyID body = scenario.world.Get<physics::PhysicsBodyComponent>(tower)->id;
    scenario.physicsWorld.Bodies().AddImpulse(body, JPH::Vec3(150000.0f, 0.0f, 0.0f), JPH::RVec3(1.0f, 8.0f, 1.0f));

    const double initial = scenario.DynamicEnergy(id);
    double injected = 0.0;
    double peakExcess = 0.0;
    for (int frame = 0; frame < kFiveSeconds; ++frame)
    {
        scenario.destruction.Update(scenario.world, scenario.jobs);
        injected += scenario.destruction.LastStats().injectedEnergy;
        scenario.sync.Step(scenario.world, scenario.jobs, kScenarioStep);
        const double energy = scenario.DynamicEnergy(id);
        ASSERT_TRUE(std::isfinite(energy)) << "frame " << frame;
        peakExcess = std::max(peakExcess, energy - (initial + injected));
    }
    EXPECT_LE(peakExcess, 0.01 * initial) << "initial " << initial << " J, injected " << injected << " J";
    // It did fall: something hit the ground hard enough to break more bonds.
    std::uint32_t broken = 0;
    for (std::uint32_t bond = 0; bond < asset.bonds.size(); ++bond)
    {
        broken += scenario.destruction.IsBondBroken(id, bond) ? 1u : 0u;
    }
    EXPECT_GT(broken, 4u);
}

TEST(Scenario, CascadeLimit_SpreadsBreaksOverFrames)
{
    // Two parallel cantilever rows (z = 0, 1) bonded side by side. Row 0's root is weak and breaks first; then
    // row 1's root carries the whole arm and breaks in the next round.
    std::vector<Cell> cells;
    for (int z = 0; z < 2; ++z)
    {
        for (int x = 0; x <= 5; ++x)
        {
            cells.push_back({x, 3, z});
        }
    }
    GridAssetOptions options;
    options.anchored = [](const Cell& cell) { return cell[0] == 0; };
    DestructibleAsset asset = MakeGridAsset(cells, options);
    const std::uint32_t root0 = *FindBond(asset, CellIndex(cells, {0, 3, 0}), CellIndex(cells, {1, 3, 0}));
    const std::uint32_t root1 = *FindBond(asset, CellIndex(cells, {0, 3, 1}), CellIndex(cells, {1, 3, 1}));
    // Intact, each root carries about half the arm.
    StressSolver probe(MakeStressGraph(asset));
    JobSystem probeJobs(physics::testing::Workers(1));
    ASSERT_TRUE(probe.Solve(probeJobs, 100000).converged);
    const BondLoad half = ToBondLoad(probe.Forces(root1), 1.0f);
    const float halfLoad = std::max({half.compression, half.tension, half.shear});
    for (const auto& [bond, scale] : {std::pair{root0, 0.8f}, std::pair{root1, 1.5f}})
    {
        asset.bonds[bond].compressiveLimit = halfLoad * scale;
        asset.bonds[bond].tensileLimit = halfLoad * scale;
        asset.bonds[bond].shearLimit = halfLoad * scale;
    }

    for (const std::uint32_t rounds : {1u, 4u})
    {
        DestructionSettings settings;
        settings.maxBreakRounds = rounds;
        Scenario scenario(2, settings);
        const DestructibleId id = scenario.Spawn(asset);
        scenario.destruction.Update(scenario.world, scenario.jobs);
        EXPECT_TRUE(scenario.destruction.IsBondBroken(id, root0)) << rounds;
        EXPECT_LE(scenario.destruction.LastStats().breakRounds, rounds);
        if (rounds == 1)
        {
            EXPECT_FALSE(scenario.destruction.IsBondBroken(id, root1)) << "the cascade waits for the next frame";
            scenario.destruction.Update(scenario.world, scenario.jobs);
        }
        EXPECT_TRUE(scenario.destruction.IsBondBroken(id, root1)) << rounds;
    }
}
