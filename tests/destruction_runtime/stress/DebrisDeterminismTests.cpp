// SPDX-License-Identifier: MIT
// A collapse whose small pieces go through the debris system (eviction, particles, fading, rubble): identical at any
// worker count.
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/testing/StateHasher.h>

#include "../../physics/PhysicsTestUtil.h"
#include "../ScenarioUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;
using namespace interi::destruction::testing;

struct DebrisRun
{
    std::vector<std::uint64_t> hashes;
    DebrisStats totals;
};

DebrisSettings Settings()
{
    DebrisSettings settings;
    settings.maxBodies = 24;
    settings.maxParticles = 12;
    settings.policy.particleMaxVolume = 1.5f;
    settings.policy.particleDistance = 13.0f;
    settings.policy.particleLifetime = 1.0f;
    settings.policy.fadeMaxVolume = 1.5f;
    settings.policy.fadeDelay = 4.0f;
    settings.policy.fadeDuration = 0.5f;
    settings.rubbleMinPieces = 2;
    settings.rubbleCellSize = 3.0f;
    return settings;
}

std::uint64_t HashDebris(const World& world, const physics::PhysicsWorld& physicsWorld)
{
    interi::testing::StateHasher hasher;
    hasher.Add(physics::testing::HashState(physicsWorld));
    Query query;
    query.With<DebrisComponent, Transform>();
    query.ForEachChunk(const_cast<World&>(world), RunTicks{.lastRun = 0, .thisRun = world.CurrentTick()},
                       [&](ChunkView& chunk)
                       {
                           const auto entities = chunk.Entities();
                           const auto debris = chunk.Read<DebrisComponent>();
                           const auto poses = chunk.Read<Transform>();
                           for (std::uint32_t row = 0; row < chunk.Count(); ++row)
                           {
                               hasher.Add(entities[row].Packed());
                               // Field by field: DebrisComponent has padding.
                               const DebrisComponent& d = debris[row];
                               hasher.Add(d.slot.index);
                               hasher.Add(d.slot.generation);
                               for (const float value : {d.localCenter.x, d.localCenter.y, d.localCenter.z, d.radius,
                                                         d.volume, d.age, d.distance, d.opacity})
                               {
                                   hasher.Add(value);
                               }
                               hasher.Add(static_cast<std::uint32_t>(d.state));
                               hasher.Add(static_cast<std::uint32_t>(d.action));
                               hasher.Add(static_cast<std::uint32_t>(d.visible));
                               hasher.AddBytes(std::as_bytes(std::span(&poses[row], 1)));
                           }
                       });
    Query rubble;
    rubble.With<RubbleComponent>();
    rubble.ForEachChunk(const_cast<World&>(world), RunTicks{.lastRun = 0, .thisRun = world.CurrentTick()},
                        [&](ChunkView& chunk)
                        {
                            for (const RubbleComponent& pile : chunk.Read<RubbleComponent>())
                            {
                                hasher.Add(pile.pieces);
                            }
                        });
    return hasher.Finish();
}

DebrisRun RunDebrisScenario(std::uint32_t workers)
{
    DestructionSettings destructionSettings;
    destructionSettings.smallPieceVolume = 1.5f; // single cubes become debris
    Scenario scenario(workers, destructionSettings);
    DebrisSystem debris(scenario.physicsWorld, Settings());
    scenario.destruction.SetDebrisSystem(&debris);
    (void)scenario.Spawn(WithConcreteLimits(MakeGridAsset(TwoStoryBuildingCells(), Concrete())));

    DebrisRun run;
    const auto step = [&](int frame)
    {
        scenario.Step();
        // The view circles the building, so pieces cross the particle distance both ways.
        const float t = static_cast<float>(frame) * kScenarioStep;
        const DebrisView view{.position = Vec3(2.0f + 12.0f * std::cos(t), 3.0f, 2.0f + 12.0f * std::sin(t)),
                              .cullPlanes = {}};
        debris.Update(scenario.world, scenario.jobs, view, kScenarioStep);
        const DebrisStats& stats = debris.LastStats();
        run.totals.adopted += stats.adopted;
        run.totals.evicted += stats.evicted;
        run.totals.toParticles += stats.toParticles;
        run.totals.removed += stats.removed;
        run.totals.rubblePiles += stats.rubblePiles;
    };
    step(0);
    for (const Vec3 center : {Vec3(1.0f, 1.5f, 1.0f), Vec3(3.0f, 1.5f, 3.0f), Vec3(2.0f, 4.5f, 2.0f)})
    {
        scenario.destruction.QueueDamage({.center = center, .energy = 1.0e7f, .innerRadius = 1.5f, .radius = 2.5f},
                                         DamageKind::Explosion);
    }
    for (int frame = 1; frame <= 360; ++frame)
    {
        step(frame);
        if (frame % 60 == 0)
        {
            run.hashes.push_back(HashDebris(scenario.world, scenario.physicsWorld));
        }
    }
    return run;
}

} // namespace

TEST(DebrisDeterminism, DebrisUpdate_ThreadCountIndependent_SameResult)
{
    const DebrisRun one = RunDebrisScenario(1);
    // The scenario exercises every debris path.
    EXPECT_GT(one.totals.adopted, 0u);
    EXPECT_GT(one.totals.evicted, 0u);
    EXPECT_GT(one.totals.toParticles, 0u);
    EXPECT_GT(one.totals.removed, 0u);
    EXPECT_GT(one.totals.rubblePiles, 0u);
    EXPECT_EQ(one.hashes, RunDebrisScenario(4).hashes);
    EXPECT_EQ(one.hashes, RunDebrisScenario(32).hashes);
}
