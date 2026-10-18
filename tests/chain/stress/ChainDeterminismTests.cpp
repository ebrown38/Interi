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
