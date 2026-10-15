// SPDX-License-Identifier: MIT
// Physics on the engine's job system with 7 workers (the console budget proxy): a second of falling debris through
// PhysicsSync, and the transform pull over a world of sleeping debris (the common case after a collapse settles).
#include <interi/core/math/Transform.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>

namespace
{

using namespace interi;
using namespace interi::physics;

constexpr float kDt = 1.0f / 60.0f;

void EnsureJolt()
{
    static const JoltRuntime runtime; // lives until exit, after every benchmark's PhysicsWorld
}

JobSystemDesc SevenWorkers()
{
    JobSystemDesc desc;
    desc.workerCount = 7;
    return desc;
}

/// `count` debris boxes in a loose grid above a ground box; every fourth is large Debris, the rest rubble.
void SpawnDebris(World& world, std::int64_t count, bool asleep)
{
    RigidBodyComponent ground;
    ground.motion = MotionType::Static;
    ground.layer = ObjectLayer::Static;
    Transform groundTransform;
    groundTransform.position = Vec3(0.0f, -1.0f, 0.0f);
    (void)world.CreateEntity(ground, ColliderComponent::Box(Vec3(100.0f, 1.0f, 100.0f)), groundTransform);

    const ColliderComponent box = ColliderComponent::Box(Vec3(0.25f));
    for (std::int64_t i = 0; i < count; ++i)
    {
        RigidBodyComponent body;
        body.layer = i % 4 == 0 ? ObjectLayer::Debris : ObjectLayer::DebrisSmall;
        body.startAsleep = asleep;
        Transform transform;
        transform.position = Vec3(static_cast<float>(i % 32) * 0.6f - 9.6f, 0.3f + static_cast<float>(i / 1024) * 0.6f,
                                  static_cast<float>((i / 32) % 32) * 0.6f - 9.6f);
        (void)world.CreateEntity(body, box, transform);
    }
}

void BM_PhysicsSync_FallingDebrisSecond(benchmark::State& state)
{
    EnsureJolt();
    JobSystem jobs(SevenWorkers());
    for (auto _ : state)
    {
        state.PauseTiming();
        auto physics = std::make_unique<PhysicsWorld>(jobs);
        auto world = std::make_unique<World>();
        auto sync = std::make_unique<PhysicsSync>(*physics);
        SpawnDebris(*world, state.range(0), false);
        sync->CreateBodies(*world);
        physics->OptimizeBroadPhase();
        state.ResumeTiming();

        for (int step = 0; step < 60; ++step)
        {
            sync->Step(*world, jobs, kDt);
        }

        state.PauseTiming();
        sync.reset();
        world.reset();
        physics.reset();
        state.ResumeTiming();
    }
    state.counters["steps"] = benchmark::Counter(60.0, benchmark::Counter::kIsIterationInvariantRate);
}
BENCHMARK(BM_PhysicsSync_FallingDebrisSecond)->Arg(1024)->Arg(4096)->Unit(benchmark::kMillisecond);

void BM_PhysicsSync_PullSleepingDebris(benchmark::State& state)
{
    EnsureJolt();
    JobSystem jobs(SevenWorkers());
    PhysicsWorld physics(jobs);
    World world;
    PhysicsSync sync(physics);
    SpawnDebris(world, state.range(0), true);
    sync.CreateBodies(world);
    for (auto _ : state)
    {
        sync.PullTransforms(world, jobs);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_PhysicsSync_PullSleepingDebris)->Arg(4096)->Arg(32768)->Unit(benchmark::kMicrosecond);

} // namespace
