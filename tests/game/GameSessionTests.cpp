// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/game/GameSession.h>
#include <interi/nav/NavAgents.h>
#include <interi/scene/Hierarchy.h>
#include <interi/testing/ScopedTempDir.h>

#include "../nav/NavTestUtil.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace interi;
namespace fs = std::filesystem;

namespace
{

void Write(const fs::path& path, std::string_view text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

constexpr std::string_view kProject = R"json({"version": 1, "name": "Tiny", "modules": [],
  "content": ["content"], "startupScene": "content/scenes/main.scene.json"})json";

constexpr std::string_view kScene = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Chains": {"chains": ["content/chains/move.chain.json", "content/chains/bad.chain.json"]}}},
  {"id": 2, "components": {"interi.LocalTransform": {"position": [0, 3, 0]}}}]})json";

constexpr std::string_view kMove = R"json({"version": 1, "nodes": [
  {"id": "go", "event": "trigger.InputAction", "params": {"key": {"const": "go"}}},
  {"id": "move", "event": "world.Move", "params": {"delta": {"const": [1.0, 0.0, 0.0]}}}],
  "links": [{"from": "go", "to": "move"}]})json";

constexpr std::string_view kBad =
    R"json({"version": 1, "nodes": [{"id": "x", "event": "no.Such.Event"}], "links": []})json";

class GameSessionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Write(m_temp.Path() / "project.json", kProject);
        Write(m_temp.Path() / "content/scenes/main.scene.json", kScene);
        Write(m_temp.Path() / "content/chains/move.chain.json", kMove);
        Write(m_temp.Path() / "content/chains/bad.chain.json", kBad);
    }

    game::GameDesc Desc() const
    {
        game::GameDesc desc;
        desc.projectDirectory = m_temp.Path();
        desc.workerCount = 1;
        return desc;
    }

    interi::testing::ScopedTempDir m_temp;
};

} // namespace

TEST_F(GameSessionTest, Open_RunsTheSceneAndReportsChainsThatDidNotCompile)
{
    auto session = game::GameSession::Open(Desc());
    ASSERT_TRUE(session.has_value()) << session.error().message;
    ASSERT_EQ((*session)->Warnings().size(), 1u);
    EXPECT_NE((*session)->Warnings()[0].find("bad.chain.json"), std::string::npos);
    (*session)->RaiseInputAction("go");
    (*session)->Step();
    (*session)->Step();
    const auto& nodes = scene::BuildHierarchy((*session)->GetWorld()).nodes;
    EXPECT_EQ((*session)->GetWorld().Get<Transform>(nodes.at(1).entity)->position, Vec3(1, 0, 0));
    EXPECT_EQ((*session)->GetWorld().Get<Transform>(nodes.at(2).entity)->position, Vec3(0, 3, 0));
}

TEST_F(GameSessionTest, Open_Failures_ReportWhatIsWrong)
{
    game::GameDesc missing = Desc();
    missing.projectDirectory = m_temp.Path() / "nowhere";
    EXPECT_FALSE(game::GameSession::Open(missing).has_value());

    game::GameDesc otherScene = Desc();
    otherScene.scene = "content/scenes/nope.scene.json";
    EXPECT_EQ(game::GameSession::Open(otherScene).error().code, ErrorCode::NotFound);

    Write(m_temp.Path() / "project.json", R"json({"version": 1, "name": "No scene"})json");
    EXPECT_EQ(game::GameSession::Open(Desc()).error().code, ErrorCode::InvalidArgument);

    Write(
        m_temp.Path() / "project.json",
        R"json({"version": 1, "name": "x", "modules": ["com.unknown"], "startupScene": "content/scenes/main.scene.json"})json");
    EXPECT_FALSE(game::GameSession::Open(Desc()).has_value());
}

TEST_F(GameSessionTest, Open_InvalidProjectData_FailsTheOpen)
{
    Write(
        m_temp.Path() / "project.json",
        R"json({"version": 1, "name": "x", "data": "data.json", "startupScene": "content/scenes/main.scene.json"})json");
    Write(m_temp.Path() / "data.json", "{ not json");
    const auto session = game::GameSession::Open(Desc());
    ASSERT_FALSE(session.has_value());
    EXPECT_NE(session.error().message.find("data.json"), std::string::npos);
}

TEST_F(GameSessionTest, Restart_ReloadsTheScene_AndKeepsTheOldOneIfTheNewOneFails)
{
    auto session = game::GameSession::Open(Desc());
    ASSERT_TRUE(session.has_value());
    (*session)->RaiseInputAction("go");
    (*session)->Step();
    (*session)->Step();
    ASSERT_TRUE((*session)->Restart().has_value());
    {
        const auto& nodes = scene::BuildHierarchy((*session)->GetWorld()).nodes;
        EXPECT_EQ(nodes.size(), 2u);
        EXPECT_EQ((*session)->GetWorld().Get<Transform>(nodes.at(1).entity)->position, Vec3(0, 0, 0));
    }
    Write(m_temp.Path() / "content/scenes/main.scene.json", "{ broken");
    EXPECT_FALSE((*session)->Restart().has_value());
    EXPECT_EQ(scene::BuildHierarchy((*session)->GetWorld()).nodes.size(), 2u); // the running scene is untouched
    (*session)->Step();                                                        // and still runs
}

TEST_F(GameSessionTest, SaveSlots_NeedADirectory_AndRoundTrip)
{
    auto none = game::GameSession::Open(Desc());
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ((*none)->SaveToSlot(0, "x").error().code, ErrorCode::Unsupported);
    EXPECT_EQ((*none)->LoadFromSlot(0).error().code, ErrorCode::Unsupported);

    game::GameDesc desc = Desc();
    desc.saveDirectory = m_temp.Path() / "saves_dir";
    auto session = game::GameSession::Open(desc);
    ASSERT_TRUE(session.has_value());
    (*session)->RaiseInputAction("go");
    (*session)->Step();
    (*session)->Step();
    ASSERT_TRUE((*session)->SaveToSlot(2, "after one move").has_value());
    (*session)->RaiseInputAction("go");
    (*session)->Step();
    (*session)->Step();
    ASSERT_TRUE((*session)->LoadFromSlot(2).has_value());
    const auto& nodes = scene::BuildHierarchy((*session)->GetWorld()).nodes;
    EXPECT_EQ((*session)->GetWorld().Get<Transform>(nodes.at(1).entity)->position, Vec3(1, 0, 0));
    EXPECT_EQ((*session)->Slots().size(), 3u);
    EXPECT_EQ((*session)->Slots()[2].label, "after one move");
}

namespace
{

constexpr std::string_view kNavScene = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Chains": {"chains": ["content/chains/nav.chain.json"]},
                           "interi.LocalTransform": {"position": [0, 0, 0]}}}]})json";

constexpr std::string_view kNavChain = R"json({"version": 1, "nodes": [
  {"id": "start", "event": "trigger.Start"},
  {"id": "go", "event": "nav.MoveTo", "params": {"destination": {"const": [3, 0, 0]}, "speed": {"const": 6}}},
  {"id": "arrived", "event": "nav.Arrived"},
  {"id": "hide", "event": "world.Hide"},
  {"id": "failed", "event": "nav.Failed"},
  {"id": "show", "event": "world.Show"}],
  "links": [{"from": "start", "to": "go"}, {"from": "arrived", "to": "hide"}, {"from": "failed", "to": "show"}]})json";

void WriteNavMesh(const fs::path& path)
{
    interi::nav::testing::Geometry floor = interi::nav::testing::SimpleFloor();
    interi::nav::NavBakeSettings settings;
    settings.agentRadius = 0.3f;
    auto mesh = interi::nav::NavMesh::Bake(floor.vertices, floor.indices, settings);
    ASSERT_TRUE(mesh.has_value());
    const std::vector<std::byte> bytes = mesh->Write();
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_F(GameSessionTest, Navigation_ChainSendsAnAgentAndGetsTheArrivalTrigger)
{
    Write(m_temp.Path() / "content/scenes/main.scene.json", kNavScene);
    Write(m_temp.Path() / "content/chains/nav.chain.json", kNavChain);
    WriteNavMesh(m_temp.Path() / "content/navmesh.inav");
    auto session = game::GameSession::Open(Desc());
    ASSERT_TRUE(session.has_value()) << session.error().message;
    const Entity agent = scene::BuildHierarchy((*session)->GetWorld()).nodes.at(1).entity;
    ASSERT_TRUE((*session)->GetWorld().Add(agent, interi::nav::NavAgentComponent{}));

    for (int i = 0; i < 80; ++i)
    {
        (*session)->Step();
    }
    World& world = (*session)->GetWorld();
    EXPECT_TRUE(world.Has<chain::HiddenComponent>(agent)) << "nav.Arrived never fired";
    EXPECT_NEAR(world.Get<Transform>(agent)->position.x, 3.0f, 0.2f);
    // The scene's local transform was updated too, so propagation kept the agent where it walked.
    EXPECT_NEAR(world.Get<scene::LocalTransformComponent>(agent)->position.x, 3.0f, 0.2f);
    EXPECT_EQ(world.Get<interi::nav::NavAgentComponent>(agent)->state, interi::nav::AgentState::Idle);
}

TEST_F(GameSessionTest, Navigation_DamagedMeshIsAWarning_AndAgentsFail)
{
    Write(m_temp.Path() / "content/scenes/main.scene.json", kNavScene);
    Write(m_temp.Path() / "content/chains/nav.chain.json", kNavChain);
    Write(m_temp.Path() / "content/navmesh.inav", "not a navmesh");
    auto session = game::GameSession::Open(Desc());
    ASSERT_TRUE(session.has_value());
    const auto warnings = (*session)->Warnings();
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("navmesh.inav"), std::string::npos);
    const Entity agent = scene::BuildHierarchy((*session)->GetWorld()).nodes.at(1).entity;
    ASSERT_TRUE((*session)->GetWorld().Add(agent, interi::nav::NavAgentComponent{}));
    for (int i = 0; i < 5; ++i)
    {
        (*session)->Step();
    }
    EXPECT_FALSE((*session)->GetWorld().Has<chain::HiddenComponent>(agent));
    EXPECT_EQ((*session)->GetWorld().Get<interi::nav::NavAgentComponent>(agent)->state, interi::nav::AgentState::Idle);
}
