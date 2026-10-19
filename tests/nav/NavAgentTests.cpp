// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/core/math/Transform.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/nav/NavAgents.h>
#include <interi/nav/NavEvents.h>

#include "../chain/ChainTestUtil.h"
#include "NavTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>

using namespace interi;
using namespace interi::nav;
using namespace interi::nav::testing;

namespace
{

std::shared_ptr<const NavMesh> BakeShared(const Geometry& g)
{
    NavBakeSettings settings;
    settings.agentRadius = 0.3f;
    auto mesh = NavMesh::Bake(g.vertices, g.indices, settings);
    EXPECT_TRUE(mesh.has_value());
    return std::make_shared<const NavMesh>(std::move(*mesh));
}

Entity SpawnAgent(World& world, std::uint64_t id, Vec3 position, Vec3 destination, float speed = 3.0f)
{
    Transform transform;
    transform.position = position;
    NavAgentComponent agent;
    agent.destination = destination;
    agent.speed = speed;
    agent.state = AgentState::Moving;
    return world.CreateEntity(chain::StableIdComponent{.id = id}, transform, agent);
}

} // namespace

TEST(NavAgents, Step_WalksAtSpeedTowardsTheDestination)
{
    World world;
    const auto mesh = BakeShared(SimpleFloor());
    const Entity e = SpawnAgent(world, 1, Vec3(-5.0f, 0.0f, 0.0f), Vec3(5.0f, 0.0f, 0.0f));
    const NavStepResult result = StepNavAgents(world, mesh.get(), 1.0f);
    ASSERT_EQ(result.moves.size(), 1u);
    EXPECT_NEAR(world.Get<Transform>(e)->position.x, -2.0f, 0.05f);
    EXPECT_TRUE(result.arrived.empty());
    EXPECT_EQ(world.Get<NavAgentComponent>(e)->state, AgentState::Moving);
}

TEST(NavAgents, Step_ArrivesOnceThenGoesIdle)
{
    World world;
    const auto mesh = BakeShared(SimpleFloor());
    const Entity e = SpawnAgent(world, 7, Vec3(-1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
    NavStepResult result = StepNavAgents(world, mesh.get(), 1.0f);
    ASSERT_EQ(result.arrived.size(), 1u);
    EXPECT_EQ(result.arrived[0], 7u);
    EXPECT_EQ(world.Get<NavAgentComponent>(e)->state, AgentState::Idle);
    EXPECT_NEAR(world.Get<Transform>(e)->position.x, 1.0f, 0.05f);
    result = StepNavAgents(world, mesh.get(), 1.0f);
    EXPECT_TRUE(result.arrived.empty());
    EXPECT_TRUE(result.moves.empty());
}

TEST(NavAgents, Step_NoRouteOrNoMesh_Fails)
{
    World world;
    const auto mesh = BakeShared(TwoIslands());
    SpawnAgent(world, 2, Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    SpawnAgent(world, 3, Vec3(-6.0f, 0.0f, 1.0f), Vec3(-6.0f, 40.0f, 1.0f));
    NavStepResult result = StepNavAgents(world, mesh.get(), 1.0f);
    EXPECT_EQ(result.failed, (std::vector<std::uint64_t>{2, 3}));

    SpawnAgent(world, 4, Vec3(-6.0f, 0.0f, 2.0f), Vec3(-5.0f, 0.0f, 2.0f));
    result = StepNavAgents(world, nullptr, 1.0f);
    EXPECT_EQ(result.failed, (std::vector<std::uint64_t>{4}));
}

TEST(NavAgents, Step_VisitsAgentsInStableIdOrder)
{
    World world;
    const auto mesh = BakeShared(SimpleFloor());
    SpawnAgent(world, 30, Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f));
    SpawnAgent(world, 10, Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 0.0f, 1.0f));
    SpawnAgent(world, 20, Vec3(0.0f, 0.0f, 2.0f), Vec3(1.0f, 0.0f, 2.0f));
    const NavStepResult result = StepNavAgents(world, mesh.get(), 1.0f);
    EXPECT_EQ(result.arrived, (std::vector<std::uint64_t>{10, 20, 30}));
}

TEST(NavAgents, Walk_GoesAroundThePit)
{
    World world;
    const auto mesh = BakeShared(FloorWithPit());
    const Entity e = SpawnAgent(world, 1, Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f), 4.0f);
    int steps = 0;
    while (world.Get<NavAgentComponent>(e)->state == AgentState::Moving && steps < 100)
    {
        const NavStepResult result = StepNavAgents(world, mesh.get(), 0.25f);
        const Vec3 p = world.Get<Transform>(e)->position;
        EXPECT_FALSE(std::abs(p.x) < 2.8f && std::abs(p.z) < 2.8f) << p.x << ", " << p.z;
        ++steps;
    }
    EXPECT_LT(steps, 100);
    EXPECT_NEAR(world.Get<Transform>(e)->position.x, 6.0f, 0.2f);
}

namespace
{

struct NavEventFixture
{
    chain::testing::TestProject project;
    std::shared_ptr<NavSource> source = std::make_shared<NavSource>();
    World world;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    CommandBuffers commands{jobs};
    std::unique_ptr<chain::ChainExecutor> executor;

    NavEventFixture()
    {
        for (std::string& id : NavigationContracts())
        {
            project.modules.DeclareContract(std::move(id));
        }
        EXPECT_TRUE(RegisterNavigationLibrary(*project.chains, source).has_value());
        executor = std::make_unique<chain::ChainExecutor>(*project.chains, project.values.get(), chain::ExecutorDesc{});
    }

    auto Add(const std::string& json, std::uint64_t entity = 0)
    {
        const auto id = executor->AddChain(chain::testing::Chain(json), entity);
        EXPECT_TRUE(executor->IsActive(id))
            << (executor->Diagnostics(id).empty() ? "" : executor->Diagnostics(id).front().Text());
        return id;
    }
    void Step() { executor->Step(world, jobs, commands); }
    std::int32_t Score() { return std::get<std::int32_t>(*executor->Values().Read("com.test.a:Score")); }
};

} // namespace

TEST(NavEvents, FindPath_FoundCarriesDistance_NotFoundWithoutMesh)
{
    NavEventFixture f;
    const std::string chain = R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "path", "event": "nav.FindPath", "params": {"from": {"const": [-5, 0, 0]}, "to": {"const": [5, 0, 0]}}},
        {"id": "found", "event": "world.SetValue", "params": {"id": {"const": "com.test.a:Score"},
                                                              "value": {"expr": "int(payload.distance)"}}},
        {"id": "miss", "event": "world.SetValue", "params": {"id": {"const": "com.test.a:Score"},
                                                             "value": {"const": -1}}}],
        "links": [{"from": "start", "to": "path"}, {"from": "path.Found", "to": "found"},
                  {"from": "path.NotFound", "to": "miss"}]})json";
    f.Add(chain);
    f.Step();
    EXPECT_EQ(f.Score(), -1); // no mesh yet

    NavEventFixture g;
    g.source->Set(BakeShared(SimpleFloor()));
    g.Add(chain);
    g.Step();
    EXPECT_EQ(g.Score(), 10);
}

TEST(NavEvents, MoveToStopAndTriggers_DriveAnAgent)
{
    NavEventFixture f;
    const auto mesh = BakeShared(SimpleFloor());
    f.source->Set(mesh);
    NavAgentComponent agent;
    Transform transform;
    transform.position = Vec3(-1.0f, 0.0f, 0.0f);
    const Entity e = f.world.CreateEntity(chain::StableIdComponent{.id = 5}, transform, agent);
    f.Add(R"json({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "go", "event": "nav.MoveTo", "params": {"destination": {"const": [1, 0, 0]}, "speed": {"const": 2}}},
        {"id": "arrived", "event": "nav.Arrived"},
        {"id": "score", "event": "world.SetValue", "params": {"id": {"const": "com.test.a:Score"},
                                                              "value": {"const": 7}}}],
        "links": [{"from": "start", "to": "go"}, {"from": "arrived", "to": "score"}]})json",
          5);
    f.Step();
    ASSERT_EQ(f.world.Get<NavAgentComponent>(e)->state, AgentState::Moving);
    EXPECT_EQ(f.world.Get<NavAgentComponent>(e)->speed, 2.0f);

    RaiseNavTriggers(*f.executor, StepNavAgents(f.world, mesh.get(), 2.0f));
    f.Step();
    EXPECT_EQ(f.Score(), 7);
    EXPECT_EQ(f.world.Get<NavAgentComponent>(e)->state, AgentState::Idle);
}

TEST(NavEvents, Stop_IdlesTheAgent)
{
    NavEventFixture f;
    NavAgentComponent agent;
    agent.state = AgentState::Moving;
    const Entity e = f.world.CreateEntity(chain::StableIdComponent{.id = 5}, Transform{}, agent);
    f.Add(R"json({"version": 1, "nodes": [{"id": "start", "event": "trigger.Start"}, {"id": "s", "event": "nav.Stop"}],
        "links": [{"from": "start", "to": "s"}]})json",
          5);
    f.Step();
    EXPECT_EQ(f.world.Get<NavAgentComponent>(e)->state, AgentState::Idle);
}
