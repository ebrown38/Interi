// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/core/math/Transform.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include "ChainTestUtil.h"
#include <gtest/gtest.h>

#include <format>
#include <string>

using namespace interi;
using namespace interi::chain;
using namespace interi::chain::testing;
using values::Value;

namespace
{

struct WorldFixture
{
    TestProject project;
    World world;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    CommandBuffers commands{jobs};
    std::unique_ptr<ChainExecutor> executor;

    WorldFixture()
    {
        ExecutorDesc desc;
        desc.trace = true;
        executor = std::make_unique<ChainExecutor>(*project.chains, project.values.get(), std::move(desc));
    }

    Entity Spawn(std::uint64_t id, Vec3 position = {})
    {
        Transform transform;
        transform.position = position;
        return world.CreateEntity(StableIdComponent{.id = id}, transform);
    }

    void Add(const std::string& json, std::uint64_t entity = 0)
    {
        const auto chain = executor->AddChain(Chain(json), entity);
        ASSERT_TRUE(executor->IsActive(chain))
            << (executor->Diagnostics(chain).empty() ? "" : executor->Diagnostics(chain).front().Text());
    }

    void Step(int count = 1)
    {
        for (int i = 0; i < count; ++i)
        {
            executor->Step(world, jobs, commands);
        }
    }
};

/// Start -> each of `events` (JSON node bodies without ids), in order.
std::string OnStart(std::initializer_list<std::string_view> events)
{
    std::string nodes = R"json({"id": "start", "event": "trigger.Start"})json";
    std::string links;
    int index = 0;
    for (const std::string_view event : events)
    {
        nodes += std::format(R"json(, {{"id": "n{}", {}}})json", index, event);
        links += std::format(R"json({}{{"from": "start", "to": "n{}"}})json", index == 0 ? "" : ", ", index);
        ++index;
    }
    return std::format(R"json({{"version": 1, "nodes": [{}], "links": [{}]}})json", nodes, links);
}

} // namespace

TEST(WorldEvents, MoveTwiceInOneStep_BothApply)
{
    WorldFixture f;
    const Entity crate = f.Spawn(7, Vec3(1.0f, 0.0f, 0.0f));
    f.Add(OnStart({R"json("event": "world.Move", "params": {"delta": {"const": [0, 2, 0]}})json",
                   R"json("event": "world.Move", "params": {"delta": {"const": [0, 0, 3]}})json"}),
          7);
    f.Step();
    EXPECT_EQ(f.world.Get<Transform>(crate)->position, Vec3(1.0f, 2.0f, 3.0f));
}

TEST(WorldEvents, ExplicitTarget_MovesThatEntity)
{
    WorldFixture f;
    const Entity self = f.Spawn(1);
    const Entity other = f.Spawn(2);
    f.Add(OnStart(
              {R"json("event": "world.Move", "params": {"target": {"const": 2}, "delta": {"const": [5, 0, 0]}})json"}),
          1);
    f.Step();
    EXPECT_EQ(f.world.Get<Transform>(self)->position, Vec3{});
    EXPECT_EQ(f.world.Get<Transform>(other)->position, Vec3(5.0f, 0.0f, 0.0f));
}

TEST(WorldEvents, Rotate_AboutAxis)
{
    WorldFixture f;
    const Entity crate = f.Spawn(3);
    f.Add(
        OnStart(
            {R"json("event": "world.Rotate", "params": {"axis": {"const": [0, 1, 0]}, "degrees": {"const": 90}})json"}),
        3);
    f.Step();
    const Vec3 forward = Rotate(f.world.Get<Transform>(crate)->rotation, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(forward.x, 0.0f, 1e-6f);
    EXPECT_NEAR(forward.z, -1.0f, 1e-6f);
}

TEST(WorldEvents, Destroy_RemovesEntityAndRaisesDestroyed)
{
    WorldFixture f;
    const Entity doomed = f.Spawn(4);
    f.Add(OnStart({R"json("event": "world.Destroy")json"}), 4);
    f.Add(R"json({"version": 1, "nodes": [{"id": "d", "event": "trigger.Destroyed"},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"expr": "int(payload.entity == self)"}}}],
        "links": [{"from": "d", "to": "r"}]})json");
    f.Step();
    EXPECT_FALSE(f.world.IsAlive(doomed));
    EXPECT_TRUE(f.project.log->empty());
    f.Step();
    // The global chain's activation entity is the destroyed one.
    ASSERT_EQ(f.project.log->size(), 1u);
    EXPECT_EQ((*f.project.log)[0], (std::pair<std::int32_t, std::uint64_t>{1, 4}));
}

TEST(WorldEvents, HideThenShow_LastWins)
{
    WorldFixture f;
    const Entity crate = f.Spawn(5);
    f.Add(OnStart({R"json("event": "world.Hide")json"}), 5);
    f.Step();
    EXPECT_TRUE(f.world.Has<HiddenComponent>(crate));

    WorldFixture g;
    const Entity box = g.Spawn(5);
    g.Add(OnStart({R"json("event": "world.Hide")json", R"json("event": "world.Show")json"}), 5);
    g.Step();
    EXPECT_FALSE(g.world.Has<HiddenComponent>(box));
}

TEST(WorldEvents, SetAndModifyValue_TypedByTheValue)
{
    WorldFixture f;
    f.Add(OnStart(
        {R"json("event": "world.SetValue", "params": {"id": {"const": "com.test.a:Health"}, "value": {"const": 40}})json",
         R"json("event": "world.ModifyValue", "params": {"id": {"const": "com.test.a:Score"}, "delta": {"const": 5}})json",
         R"json("event": "world.ModifyValue", "params": {"id": {"const": "com.test.a:Score"}, "delta": {"expr": "2 * 3"}})json"}));
    f.Step();
    EXPECT_EQ(*f.executor->Values().Read("com.test.a:Health"), Value(40.0f));
    EXPECT_EQ(*f.executor->Values().Read("com.test.a:Score"), Value(std::int32_t{11}));

    // A value of the wrong type doesn't compile.
    const auto bad = f.executor->AddChain(Chain(OnStart(
        {R"json("event": "world.SetValue", "params": {"id": {"const": "com.test.a:Armed"}, "value": {"const": 3}})json"})));
    EXPECT_FALSE(f.executor->IsActive(bad));
    const auto unknown = f.executor->AddChain(Chain(OnStart(
        {R"json("event": "world.SetValue", "params": {"id": {"const": "com.test.a:Nope"}, "value": {"const": 3}})json"})));
    EXPECT_FALSE(f.executor->IsActive(unknown));
}

TEST(WorldEvents, PlaySound_RequestForHost)
{
    WorldFixture f;
    f.Spawn(9);
    f.Add(
        OnStart(
            {R"json("event": "world.PlaySound", "params": {"cue": {"const": "impact"}, "volume": {"const": 0.5}})json"}),
        9);
    f.Step();
    ASSERT_EQ(f.executor->Sounds().size(), 1u);
    EXPECT_EQ(f.executor->Sounds()[0].cue, "impact");
    EXPECT_EQ(f.executor->Sounds()[0].entity, 9u);
    EXPECT_EQ(f.executor->Sounds()[0].volume, 0.5f);
    f.Step();
    EXPECT_TRUE(f.executor->Sounds().empty());
}

TEST(WorldEvents, MissingTarget_Skipped)
{
    WorldFixture f;
    f.Add(
        OnStart({R"json("event": "world.Move", "params": {"target": {"const": 99}, "delta": {"const": [1, 0, 0]}})json",
                 R"json("event": "world.Destroy", "params": {"target": {"const": 99}})json"}));
    f.Step();
    EXPECT_EQ(f.executor->LastStats().eventsRun, 2u);
}

TEST(WorldEvents, Trace_RecordsFiredConditionAndRan)
{
    WorldFixture f;
    f.Add(R"json({"version": 1, "nodes": [{"id": "start", "event": "trigger.Start"},
        {"id": "r", "event": "com.test.a:Record", "params": {"tag": {"const": 3}}}],
        "links": [{"from": "start", "to": "r", "condition": "true"}]})json");
    f.Step();
    const auto& trace = f.executor->Trace();
    ASSERT_EQ(trace.size(), 4u);
    EXPECT_EQ(trace[0].kind, TraceRecord::Kind::Fired);
    EXPECT_EQ(trace[0].node, "start");
    EXPECT_EQ(trace[0].output, "Fired");
    EXPECT_EQ(trace[1].kind, TraceRecord::Kind::Condition);
    EXPECT_TRUE(trace[1].passed);
    EXPECT_EQ(trace[2].kind, TraceRecord::Kind::Ran);
    EXPECT_EQ(trace[2].node, "r");
    EXPECT_EQ(trace[2].payload, (std::vector<Value>{std::int32_t{3}}));
    // Record's output fires too, though nothing is linked to it.
    EXPECT_EQ(trace[3].kind, TraceRecord::Kind::Fired);
    EXPECT_EQ(trace[3].node, "r");
}
