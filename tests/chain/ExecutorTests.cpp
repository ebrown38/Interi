// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include "ChainTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace interi;
using namespace interi::chain;
using namespace interi::chain::testing;
using values::Value;

namespace
{

using Log = std::vector<std::pair<std::int32_t, std::uint64_t>>;

struct Fixture
{
    TestProject project;
    World world;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    CommandBuffers commands{jobs};
    std::unique_ptr<ChainExecutor> executor;

    explicit Fixture(const std::function<void(ExecutorDesc&)>& configure = {})
    {
        ExecutorDesc desc;
        if (configure)
        {
            configure(desc);
        }
        executor = std::make_unique<ChainExecutor>(*project.chains, project.values.get(), std::move(desc));
    }

    void Step(int count = 1)
    {
        for (int i = 0; i < count; ++i)
        {
            executor->Step(world, jobs, commands);
        }
    }

    ChainExecutor::ChainId Add(std::string_view json, std::uint64_t entity = 0)
    {
        const auto id = executor->AddChain(Chain(json), entity);
        EXPECT_TRUE(executor->IsActive(id))
            << (executor->Diagnostics(id).empty() ? std::string() : executor->Diagnostics(id).front().Text());
        return id;
    }

    [[nodiscard]] std::int32_t Score() const
    {
        return std::get<std::int32_t>(*executor->Values().Read("com.test.a:Score"));
    }
};

/// A chain: `trigger` -> Record(tag).
std::string RecordOn(std::string_view trigger, int tag, int priority = 0, std::string_view triggerParams = "{}")
{
    return std::format(R"json({{"version": 1, "priority": {}, "nodes": [
        {{"id": "t", "event": "{}", "params": {}}},
        {{"id": "r", "event": "com.test.a:Record", "params": {{"tag": {{"const": {}}}}}}}],
        "links": [{{"from": "t", "to": "r"}}]}})json",
                       priority, trigger, triggerParams, tag);
}

} // namespace

TEST(Executor, Start_FiresOnceOnFirstStep)
{
    Fixture f;
    f.Add(RecordOn("trigger.Start", 7));
    f.Step(3);
    EXPECT_EQ(*f.project.log, (Log{{7, 0}}));

    // A chain added later starts on the next step.
    f.Add(RecordOn("trigger.Start", 8));
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{7, 0}, {8, 0}}));
}

TEST(Executor, Order_ByPriorityThenEntityThenSequence)
{
    Fixture f;
    f.Add(RecordOn("trigger.Start", 1, 5), 20);
    f.Add(RecordOn("trigger.Start", 2, 5), 10);
    f.Add(RecordOn("trigger.Start", 3, -1), 30);
    f.Add(RecordOn("trigger.Start", 4, 5), 10);
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{3, 30}, {2, 10}, {4, 10}, {1, 20}}));
}

TEST(Executor, Conditions_ReadPreStepState)
{
    Fixture f;
    // Start -> AddScore; Start -> (if Score == 0) Record(1); AddScore.Out -> (if Score == 0) Record(2). All in one
    // step: every condition sees the pre-step score, though AddScore ran first.
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "add", "event": "com.test.a:AddScore"},
        {"id": "r1", "event": "com.test.a:Record", "params": {"tag": {"const": 1}}},
        {"id": "r2", "event": "com.test.a:Record", "params": {"tag": {"const": 2}}}],
        "links": [{"from": "start", "to": "add"},
                  {"from": "start", "to": "r1", "condition": "value(\"com.test.a:Score\") == 0"},
                  {"from": "add", "to": "r2", "condition": "value(\"com.test.a:Score\") == 0"}]})json");
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{1, 0}, {2, 0}}));
    EXPECT_EQ(f.Score(), 1);
}

TEST(Executor, TwoDeltasInOneStep_BothApply)
{
    Fixture f;
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "a", "event": "com.test.a:AddScore", "params": {"delta": {"const": 3}}},
        {"id": "b", "event": "com.test.a:AddScore", "params": {"delta": {"const": -10}}}],
        "links": [{"from": "start", "to": "a"}, {"from": "start", "to": "b"}]})json");
    f.Step();
    EXPECT_EQ(f.Score(), -7);
}

TEST(Executor, ValueChanged_FiresNextStep)
{
    Fixture f;
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "add", "event": "com.test.a:AddScore"},
        {"id": "changed", "event": "trigger.ValueChanged", "params": {"key": {"const": "com.test.a:Score"}}},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"expr": "value(\"com.test.a:Score\")"}}}],
        "links": [{"from": "start", "to": "add"}, {"from": "changed", "to": "r"}]})json");
    f.Step();
    EXPECT_TRUE(f.project.log->empty());
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{1, 0}}));
}

TEST(Executor, Delay_FiresAfterWholeSteps)
{
    Fixture f([](ExecutorDesc& d) { d.stepNanoseconds = 10'000'000; });
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "wait", "event": "flow.Delay", "params": {"seconds": {"const": 0.03}}},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"const": 1}}}],
        "links": [{"from": "start", "to": "wait"}, {"from": "wait", "to": "r"}]})json");
    f.Step(); // step 0: Delay runs
    EXPECT_EQ(f.executor->PendingContinuations(), 1u);
    f.Step(2);
    EXPECT_TRUE(f.project.log->empty());
    f.Step(); // step 3
    EXPECT_EQ(f.project.log->size(), 1u);
    EXPECT_EQ(f.executor->PendingContinuations(), 0u);
}

TEST(Executor, FlowTimer_FiresCountTimes)
{
    Fixture f([](ExecutorDesc& d) { d.stepNanoseconds = 10'000'000; });
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "timer", "event": "flow.Timer", "params": {"interval": {"const": 0.02}, "count": {"const": 3}}},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"payload": "count"}}}],
        "links": [{"from": "start", "to": "timer"}, {"from": "timer", "to": "r"}]})json");
    f.Step(10);
    EXPECT_EQ(*f.project.log, (Log{{1, 0}, {2, 0}, {3, 0}}));
}

TEST(Executor, TickAndTimerTriggers)
{
    Fixture f([](ExecutorDesc& d) { d.stepNanoseconds = 10'000'000; });
    f.Add(RecordOn("trigger.Tick", 1));
    f.Add(RecordOn("trigger.Timer", 2, 0, R"json({"interval": {"const": 0.03}})json"));
    f.Step(7);
    std::size_t ticks = 0;
    std::size_t timers = 0;
    for (const auto& [tag, entity] : *f.project.log)
    {
        (tag == 1 ? ticks : timers) += 1;
    }
    EXPECT_EQ(ticks, 7u);
    EXPECT_EQ(timers, 2u); // steps 3 and 6
}

TEST(Executor, BranchSequenceRandom)
{
    Fixture f([](ExecutorDesc& d) { d.seed = 42; });
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "seq", "event": "flow.Sequence"},
        {"id": "branch", "event": "flow.Branch", "params": {"condition": {"const": false}}},
        {"id": "rand", "event": "flow.Random", "params": {"min": {"const": 10}, "max": {"const": 20}}},
        {"id": "r0", "event": "com.test.a:Record", "params": {"tag": {"const": 0}}},
        {"id": "r1", "event": "com.test.a:Record", "params": {"tag": {"const": 1}}},
        {"id": "rf", "event": "com.test.a:Record", "params": {"tag": {"const": 100}}},
        {"id": "rr", "event": "com.test.a:Record", "params": {"tag": {"payload": "value"}}}],
        "links": [{"from": "start", "to": "seq"}, {"from": "seq.Then1", "to": "r1"}, {"from": "seq.Then0", "to": "r0"},
                  {"from": "seq.Then2", "to": "branch"}, {"from": "branch.False", "to": "rf"},
                  {"from": "seq.Then3", "to": "rand"}, {"from": "rand", "to": "rr"}]})json");
    f.Step();
    ASSERT_EQ(f.project.log->size(), 4u);
    EXPECT_EQ((*f.project.log)[0].first, 0);
    EXPECT_EQ((*f.project.log)[1].first, 1);
    EXPECT_EQ((*f.project.log)[2].first, 100);
    const std::int32_t value = (*f.project.log)[3].first;
    EXPECT_GE(value, 10);
    EXPECT_LE(value, 20);

    // Same seed, same draw.
    Fixture again([](ExecutorDesc& d) { d.seed = 42; });
    again.Add(WriteChain(Chain(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "seq", "event": "flow.Sequence"},
        {"id": "branch", "event": "flow.Branch", "params": {"condition": {"const": false}}},
        {"id": "rand", "event": "flow.Random", "params": {"min": {"const": 10}, "max": {"const": 20}}},
        {"id": "r0", "event": "com.test.a:Record", "params": {"tag": {"const": 0}}},
        {"id": "r1", "event": "com.test.a:Record", "params": {"tag": {"const": 1}}},
        {"id": "rf", "event": "com.test.a:Record", "params": {"tag": {"const": 100}}},
        {"id": "rr", "event": "com.test.a:Record", "params": {"tag": {"payload": "value"}}}],
        "links": [{"from": "start", "to": "seq"}, {"from": "seq.Then1", "to": "r1"}, {"from": "seq.Then0", "to": "r0"},
                  {"from": "seq.Then2", "to": "branch"}, {"from": "branch.False", "to": "rf"},
                  {"from": "seq.Then3", "to": "rand"}, {"from": "rand", "to": "rr"}]})json")));
    again.Step();
    ASSERT_EQ(again.project.log->size(), 4u);
    EXPECT_EQ((*again.project.log)[3].first, value);
}

TEST(Executor, InfiniteLoop_StopsAtBudget)
{
    Fixture f(
        [](ExecutorDesc& d)
        {
            d.maxActivationsPerStep = 500;
            d.maxWavesPerStep = 64;
        });
    // Echo fans out to itself twice: 2, 4, 8, ... activations per wave.
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "echo", "event": "com.test.a:Echo"}],
        "links": [{"from": "start", "to": "echo"}, {"from": "echo", "to": "echo"}, {"from": "echo", "to": "echo"}]})json");
    f.Step();
    EXPECT_TRUE(f.executor->LastStats().budgetExceeded);
    EXPECT_EQ(f.executor->LastStats().activations, 500u);
    EXPECT_GT(f.executor->LastStats().dropped, 0u);
    // Nothing carries over: the next step is quiet.
    f.Step();
    EXPECT_EQ(f.executor->LastStats().activations, 0u);
}

TEST(Executor, SelfLoop_StopsAtDepth)
{
    Fixture f(
        [](ExecutorDesc& d)
        {
            d.maxActivationsPerStep = 100000;
            d.maxWavesPerStep = 16;
        });
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "echo", "event": "com.test.a:Echo"}],
        "links": [{"from": "start", "to": "echo"}, {"from": "echo", "to": "echo"}]})json");
    f.Step();
    EXPECT_TRUE(f.executor->LastStats().depthExceeded);
    EXPECT_EQ(f.executor->LastStats().waves, 16u);
    EXPECT_EQ(f.executor->LastStats().activations, 16u);
}

TEST(Executor, InputAction_MatchesKey)
{
    Fixture f;
    f.Add(RecordOn("trigger.InputAction", 1, 0, R"json({"key": {"const": "input.Jump"}})json"));
    f.Add(RecordOn("trigger.InputAction", 2, 0, R"json({"key": {"const": "input.Fire"}})json"));
    f.Step();
    f.executor->RaiseInputAction("input.Fire");
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{2, 0}}));
}

TEST(Executor, EntityChains_ReactToTheirEntityAndGlobalTriggers)
{
    Fixture f;
    f.Add(RecordOn("trigger.Spawned", 1), 5);
    f.Add(RecordOn("trigger.Spawned", 2), 6);
    f.Add(RecordOn("trigger.Spawned", 3)); // global: every entity
    f.Step();
    f.executor->RaiseSpawned(6);
    f.Step();
    // Same priority and entity: chain order decides.
    EXPECT_EQ(*f.project.log, (Log{{2, 6}, {3, 6}}));
}

TEST(Executor, ClientRole_SkipsServerEvents)
{
    Fixture f(
        [](ExecutorDesc& d)
        {
            d.role = ExecutorRole::Client;
            d.trace = true;
        });
    EventDefinition serverOnly = Action({}, {{.name = "Out", .payload = {}}}, [](EventCall& call) { call.Fire(0); });
    serverOnly.authority = Authority::Server;
    ASSERT_TRUE(f.project.chains->Events().Provide("com.test.a", "com.test.a:ServerOnly", serverOnly).has_value());
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "s", "event": "com.test.a:ServerOnly"},
        {"id": "r", "event": "com.test.a:Record"}],
        "links": [{"from": "start", "to": "s"}, {"from": "s", "to": "r"}, {"from": "start", "to": "r"}]})json");
    f.Step();
    EXPECT_EQ(f.project.log->size(), 1u);
    EXPECT_TRUE(std::ranges::any_of(f.executor->Trace(), [](const TraceRecord& record)
                                    { return record.kind == TraceRecord::Kind::AuthoritySkip && record.node == "s"; }));
}

TEST(Executor, RegistryChange_RecompilesWithOverride)
{
    Fixture f;
    // Tick -> Shoot(range 1) -> Hit -> Record(damage)
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "tick", "event": "trigger.Tick"},
        {"id": "shoot", "event": "com.test.a:Shoot", "params": {"range": {"const": 1}}},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"expr": "int(payload.damage)"}}}],
        "links": [{"from": "tick", "to": "shoot"}, {"from": "shoot.Hit", "to": "r"}]})json");
    f.Step();
    ASSERT_TRUE(
        f.project.chains->Events().Override("com.test.plugin", "com.test.a:Shoot", ShootEvent(5.0f)).has_value());
    f.Step();
    EXPECT_EQ(*f.project.log, (Log{{2, 0}, {5, 0}}));
}

TEST(Executor, ProviderRemoved_ChainInactiveUntilRestored)
{
    Fixture f;
    const auto id = f.Add(R"json({"version": 1, "nodes": [
        {"id": "tick", "event": "trigger.Tick"},
        {"id": "e", "event": "com.test.a:Echo"}],
        "links": [{"from": "tick", "to": "e"}]})json");
    auto extracted = f.project.chains->ExtractProvider("com.test.a");
    f.Step();
    EXPECT_FALSE(f.executor->IsActive(id));
    EXPECT_FALSE(f.executor->Diagnostics(id).empty());
    f.project.chains->Restore(std::move(extracted));
    f.Step();
    EXPECT_TRUE(f.executor->IsActive(id));
    EXPECT_EQ(f.executor->LastStats().eventsRun, 1u);
}

TEST(Executor, Continuation_SurvivesRecompile)
{
    Fixture f([](ExecutorDesc& d) { d.stepNanoseconds = 10'000'000; });
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "wait", "event": "flow.Delay", "params": {"seconds": {"const": 0.02}}},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"const": 9}}}],
        "links": [{"from": "start", "to": "wait"}, {"from": "wait", "to": "r"}]})json");
    f.Step();
    // Any registry change recompiles every chain.
    ASSERT_TRUE(
        f.project.chains->Events().Override("com.test.plugin", "com.test.a:Shoot", ShootEvent(5.0f)).has_value());
    f.Step(2);
    EXPECT_EQ(*f.project.log, (Log{{9, 0}}));
}

TEST(Executor, InvalidWrite_DroppedAndTraced)
{
    Fixture f([](ExecutorDesc& d) { d.trace = true; });
    f.project.AddValue("com.test.a:Ratio", values::ValueType::Float, 0.5f);
    EventDefinition setNan =
        Action({}, {},
               [](EventCall& call)
               {
                   call.Defer([](ApplyContext& context)
                              { context.SetValue("com.test.a:Ratio", std::numeric_limits<float>::quiet_NaN()); });
               });
    ASSERT_TRUE(f.project.chains->Events().Provide("com.test.a", "com.test.a:SetNan", setNan).has_value());
    f.Add(
        R"json({"version": 1, "nodes": [{"id": "start", "event": "trigger.Start"}, {"id": "n", "event": "com.test.a:SetNan"}],
        "links": [{"from": "start", "to": "n"}]})json");
    f.Step();
    EXPECT_EQ(*f.executor->Values().Read("com.test.a:Ratio"), Value(0.5f));
    EXPECT_TRUE(std::ranges::any_of(f.executor->Trace(), [](const TraceRecord& record)
                                    { return record.kind == TraceRecord::Kind::WriteRejected; }));
}
