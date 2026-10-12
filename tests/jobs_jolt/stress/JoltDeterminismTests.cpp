// SPDX-License-Identifier: MIT
// A HelloWorld-style Jolt simulation stepped through the Interi adapter must end in exactly the same state as when
// stepped through Jolt's own JPH::JobSystemThreadPool, whatever the thread count.
#include <interi/jobs/JobSystem.h>
#include <interi/jobs_jolt/JoltJobSystem.h>
#include <interi/testing/StateHasher.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
// clang-format on

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace
{

namespace Layers
{
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
} // namespace Layers

namespace BroadPhaseLayers
{
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kCount = 2;
} // namespace BroadPhaseLayers

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a == Layers::kMoving || b == Layers::kMoving;
    }
};

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::kNonMoving ? BroadPhaseLayers::kNonMoving : BroadPhaseLayers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::kNonMoving ? "NonMoving" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broadPhase) const override
    {
        return object == Layers::kMoving || broadPhase == BroadPhaseLayers::kMoving;
    }
};

/// Drops a pile of boxes and spheres onto a floor, steps it and returns a hash of the saved physics state.
std::uint64_t SimulatePile(JPH::JobSystem& jobSystem, int steps = 90)
{
    constexpr int kGrid = 8;
    constexpr int kLayers = 4;

    const BroadPhaseLayerInterface broadPhaseLayers;
    const ObjectVsBroadPhaseLayerFilter objectVsBroadPhase;
    const ObjectLayerPairFilter objectPairs;
    JPH::PhysicsSystem physics;
    physics.Init(1024, 0, 4096, 4096, broadPhaseLayers, objectVsBroadPhase, objectPairs);
    JPH::BodyInterface& bodies = physics.GetBodyInterface();

    bodies.CreateAndAddBody(JPH::BodyCreationSettings(new JPH::BoxShape(JPH::Vec3(50.0f, 1.0f, 50.0f)),
                                                      JPH::RVec3(0.0, -1.0, 0.0), JPH::Quat::sIdentity(),
                                                      JPH::EMotionType::Static, Layers::kNonMoving),
                            JPH::EActivation::DontActivate);
    const JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(JPH::Vec3(0.4f, 0.4f, 0.4f));
    const JPH::RefConst<JPH::Shape> sphere = new JPH::SphereShape(0.45f);
    for (int layer = 0; layer < kLayers; ++layer)
    {
        for (int x = 0; x < kGrid; ++x)
        {
            for (int z = 0; z < kGrid; ++z)
            {
                // Offset alternate layers so bodies land on each other and topple.
                const auto fx = static_cast<float>(x);
                const auto fz = static_cast<float>(z);
                const float offset = static_cast<float>(layer % 2) * 0.45f;
                const JPH::RVec3 position(fx * 0.9f + offset, 1.0f + static_cast<float>(layer) * 1.1f,
                                          fz * 0.9f + offset);
                const bool useSphere = (x + z + layer) % 3 == 0;
                bodies.CreateAndAddBody(JPH::BodyCreationSettings(useSphere ? sphere : box, position,
                                                                  JPH::Quat::sRotation(JPH::Vec3::sAxisY(), 0.1f * fx),
                                                                  JPH::EMotionType::Dynamic, Layers::kMoving),
                                        JPH::EActivation::Activate);
            }
        }
    }
    physics.OptimizeBroadPhase();

    JPH::TempAllocatorImpl tempAllocator(16 * 1024 * 1024);
    for (int step = 0; step < steps; ++step)
    {
        const JPH::EPhysicsUpdateError error = physics.Update(1.0f / 60.0f, 1, &tempAllocator, &jobSystem);
        EXPECT_EQ(error, JPH::EPhysicsUpdateError::None);
    }

    JPH::StateRecorderImpl recorder;
    physics.SaveState(recorder);
    const std::string state = recorder.GetData();
    interi::testing::StateHasher hasher;
    hasher.AddBytes(std::as_bytes(std::span<const char>(state.data(), state.size())));
    return hasher.Finish();
}

std::uint64_t SimulateWithThreadPool(int threads, int steps = 90)
{
    JPH::JobSystemThreadPool pool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads - 1);
    return SimulatePile(pool, steps);
}

std::uint64_t SimulateWithInteri(std::uint32_t threads)
{
    interi::JobSystemDesc desc;
    desc.workerCount = threads - 1;
    interi::JobSystem jobs(desc);
    interi::JoltJobSystem jolt(jobs);
    return SimulatePile(jolt);
}

} // namespace

// The hash must actually capture the simulation, or the comparisons below would pass trivially.
TEST(JoltDeterminism, StateHash_ChangesWithSimulation)
{
    EXPECT_NE(SimulateWithThreadPool(4, 89), SimulateWithThreadPool(4, 90));
}

// Baseline: Jolt's own pool is deterministic across thread counts, otherwise comparing against it proves nothing.
TEST(JoltDeterminism, ThreadPool_SameStateAt1And8Threads)
{
    EXPECT_EQ(SimulateWithThreadPool(1), SimulateWithThreadPool(8));
}

TEST(JoltDeterminism, InteriAdapter_MatchesJoltThreadPool)
{
    const std::uint64_t reference = SimulateWithThreadPool(8);
    for (const std::uint32_t threads : {1u, 4u, 8u, 32u})
    {
        EXPECT_EQ(SimulateWithInteri(threads), reference) << threads << " threads";
    }
}
