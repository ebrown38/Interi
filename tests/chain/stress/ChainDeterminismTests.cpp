// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/StateHasher.h>

#include "../ChainHash.h"
#include "../ChainTestUtil.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <ios>

using namespace interi;
using namespace interi::chain;
using namespace interi::chain::testing;

namespace
{

/// Every tick each entity's chain draws a number; above a threshold it shoots (Hit damage feeds the score with a
/// condition on the pre-step score), below it waits a random-free delay and records. Many activations per step,
/// all landing in one shared Value through deltas.
constexpr std::string_view kBusyChain = R"json({"version": 1, "nodes": [
    {"id": "tick", "event": "trigger.Tick"},
    {"id": "roll", "event": "flow.Random", "params": {"min": {"const": 0}, "max": {"const": 99}}},
    {"id": "branch", "event": "flow.Branch", "params": {"condition": {"expr": "payload.value >= 40"}}},
    {"id": "shoot", "event": "com.test.a:Shoot", "params": {"range": {"expr": "float(abs(value(\"com.test.a:Score\")) % 7) - 1"}}},
    {"id": "score", "event": "com.test.a:AddScore", "params": {"delta": {"expr": "int(payload.damage) + 1"}}},
    {"id": "wait", "event": "flow.Delay", "params": {"seconds": {"const": 0.05}}},
    {"id": "penalty", "event": "com.test.a:AddScore", "params": {"delta": {"const": -3}}},
    {"id": "seq", "event": "flow.Sequence"},
    {"id": "e1", "event": "com.test.a:Echo"},
    {"id": "e2", "event": "com.test.a:Echo"}],
  "links": [
    {"from": "tick", "to": "roll"},
    {"from": "roll", "to": "branch"},
    {"from": "branch.True", "to": "shoot"},
    {"from": "shoot.Hit", "to": "score", "condition": "value(\"com.test.a:Score\") % 2 == 0 || payload.damage > 6"},
    {"from": "shoot.Miss", "to": "seq"},
    {"from": "seq.Then0", "to": "e1"},
    {"from": "seq.Then1", "to": "e2"},
    {"from": "branch.False", "to": "wait"},
    {"from": "wait", "to": "penalty"}]})json";

std::uint64_t RunScenario(std::uint32_t threads)
{
    TestProject project;
    World world;
    JobSystem jobs(JobSystemDesc{.workerCount = threads - 1, .pinThreads = false});
    CommandBuffers commands(jobs);
    ExecutorDesc desc;
    desc.seed = 0x5EEDull;
    desc.trace = true;
    desc.grain = 4;
    desc.maxActivationsPerStep = 1u << 20;
    ChainExecutor executor(*project.chains, project.values.get(), desc);
    const ChainAsset asset = Chain(kBusyChain);
    for (std::uint64_t entity = 1; entity <= 200; ++entity)
    {
        const auto id = executor.AddChain(asset, entity);
        EXPECT_TRUE(executor.IsActive(id));
        for (const Diagnostic& diagnostic : executor.Diagnostics(id))
        {
            ADD_FAILURE() << diagnostic.Text();
        }
    }
    interi::testing::StateHasher hasher;
    for (int step = 0; step < 120; ++step)
    {
        executor.Step(world, jobs, commands);
        HashExecutor(hasher, executor);
    }
    EXPECT_GT(executor.LastStats().activations, 200u);
    return hasher.Finish();
}

} // namespace

TEST(Chain, SameInput_IdenticalHashAt1_4_32Threads)
{
    const std::uint64_t one = RunScenario(1);
    EXPECT_EQ(RunScenario(4), one);
    EXPECT_EQ(RunScenario(32), one);
}

namespace
{

/// World events only, no trigonometry: moves with arithmetic and sqrt, value writes, hides, delays.
constexpr std::string_view kWorldChain = R"json({"version": 1, "nodes": [
    {"id": "tick", "event": "trigger.Tick"},
    {"id": "roll", "event": "flow.Random", "params": {"min": {"const": -50}, "max": {"const": 50}}},
    {"id": "move", "event": "world.Move",
     "params": {"delta": {"expr": "vec3(float(payload.value) / 7, sqrt(abs(float(payload.value))) * 0.1, 1 / 3.0)"}}},
    {"id": "score", "event": "world.ModifyValue",
     "params": {"id": {"const": "com.test.a:Health"}, "delta": {"expr": "float(payload.value) * 0.01"}}},
    {"id": "hide", "event": "world.Hide"},
    {"id": "wait", "event": "flow.Delay", "params": {"seconds": {"const": 0.1}}},
    {"id": "show", "event": "world.Show"}],
  "links": [
    {"from": "tick", "to": "roll"},
    {"from": "roll", "to": "move"},
    {"from": "roll", "to": "score", "condition": "payload.value > 0"},
    {"from": "roll", "to": "hide", "condition": "payload.value < -40"},
    {"from": "hide", "to": "wait"},
    {"from": "wait", "to": "show"}]})json";

std::uint64_t RunWorldScenario(std::uint32_t threads)
{
    TestProject project;
    World world;
    JobSystem jobs(JobSystemDesc{.workerCount = threads - 1, .pinThreads = false});
    CommandBuffers commands(jobs);
    ExecutorDesc desc;
    desc.seed = 0xC4A1Bull;
    desc.trace = true;
    desc.grain = 4;
    desc.stepNanoseconds = 16'666'667;
    ChainExecutor executor(*project.chains, project.values.get(), desc);
    const ChainAsset asset = Chain(kWorldChain);
    for (std::uint64_t id = 1; id <= 64; ++id)
    {
        Transform transform;
        transform.position = Vec3(static_cast<float>(id), 0.0f, 0.0f);
        world.CreateEntity(StableIdComponent{.id = id}, transform);
        const auto chain = executor.AddChain(asset, id);
        EXPECT_TRUE(executor.IsActive(chain));
    }
    interi::testing::StateHasher hasher;
    for (int step = 0; step < 90; ++step)
    {
        executor.Step(world, jobs, commands);
        HashExecutor(hasher, executor);
    }
    HashWorld(hasher, world);
    return hasher.Finish();
}

} // namespace

TEST(ChainDeterminism, WorldEvents_IdenticalHashAt1_4_32Threads)
{
    const std::uint64_t one = RunWorldScenario(1);
    EXPECT_EQ(RunWorldScenario(4), one);
    EXPECT_EQ(RunWorldScenario(32), one);
}

// Pins chain execution bit-for-bit across compilers and platforms (Linux/Clang and Windows/MSVC in CI). Any change
// to expression evaluation, executor ordering, the flow/world library, Random or the scenario changes it.
TEST(ChainDeterminism, MatchesCrossPlatformGoldenHash)
{
    constexpr std::uint64_t kGolden = 0x6a446849bd84911eull;
    const std::uint64_t hash = RunWorldScenario(4);
    EXPECT_EQ(hash, kGolden) << std::hex << "0x" << hash;
}
