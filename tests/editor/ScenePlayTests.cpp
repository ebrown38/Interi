// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/editor/ChainDebugger.h>
#include <interi/editor/DataEditing.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/ScenePlay.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/testing/ExpectNear.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <chrono>
#include <string_view>

using namespace interi;
using namespace interi::editor;
using namespace std::chrono_literals;

namespace
{

constexpr std::string_view kJump = "com.test.a:JumpHeight";

// Moves its object +1 x every step and, on the first, sets JumpHeight to 4.
constexpr std::string_view kChain = R"json({"version": 1, "nodes": [
  {"id": "tick", "event": "trigger.Tick"},
  {"id": "move", "event": "world.Move", "params": {"delta": {"const": [1.0, 0.0, 0.0]}}},
  {"id": "start", "event": "trigger.Start"},
  {"id": "jump", "event": "world.SetValue",
   "params": {"id": {"const": "com.test.a:JumpHeight"}, "value": {"const": 4.0}}}],
  "links": [{"from": "tick", "to": "move"}, {"from": "start", "to": "jump"}]})json";

class ScenePlayTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        for (std::string& id : chain::CoreContracts())
        {
            m_modules.DeclareContract(std::move(id));
        }
        m_chains = std::make_unique<chain::ChainRegistry>(m_modules);
        ASSERT_TRUE(chain::RegisterCoreLibrary(*m_chains).has_value());
        auto project = ProjectData::Create(interi::testing::ResolveSdkTestProject());
        ASSERT_TRUE(project.has_value()) << project.error().message;
        m_project = std::move(*project);
        ASSERT_TRUE(m_files.WriteFile("chains/mover.chain.json", std::as_bytes(std::span(kChain))).has_value());

        m_mover = m_document.Scene().CreatePrimitive({}, {}, "mover");
        ASSERT_TRUE(m_document.Scene().AddComponent(m_mover, scene::ChainsComponent::kComponentName));
        auto* chains = m_document.Scene().GetWorld().GetMut<scene::ChainsComponent>(*m_document.Scene().Find(m_mover));
        chains->chains.items.emplace_back(values::AssetRef{"chains/mover.chain.json"});
    }

    std::unique_ptr<ScenePlaySession> Start(bool trace = false)
    {
        auto session =
            ScenePlaySession::Start(m_document.Scene(), *m_chains, &m_project->Data().Values(), m_files, m_jobs, trace);
        EXPECT_TRUE(session.has_value()) << session.error().message;
        return session ? std::move(*session) : nullptr;
    }

    static float X(ScenePlaySession& session, scene::ObjectId id)
    {
        World& world = session.Runtime().GetWorld();
        return world.Get<Transform>(scene::BuildHierarchy(world).nodes.at(id).entity)->position.x;
    }

    sdk::ModuleSet m_modules;
    std::unique_ptr<chain::ChainRegistry> m_chains;
    std::unique_ptr<ProjectData> m_project;
    MemoryFileSystem m_files;
    JobSystem m_jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    EditorDocument m_document;
    EditorEntityId m_mover = 0;
};

} // namespace

TEST_F(ScenePlayTest, PlayThenStop_LeavesTheSceneUntouched)
{
    const std::string before = scene::WriteScene(m_document.Scene().ToDocument());
    auto session = Start();
    ASSERT_NE(session, nullptr);
    session->Advance(101ms); // six 60 Hz steps (100 ms is just short of six)
    EXPECT_EQ(session->StepIndex(), 6u);
    EXPECT_FLOAT_EQ(X(*session, m_mover), 6.0f);
    session.reset();
    EXPECT_EQ(scene::WriteScene(m_document.Scene().ToDocument()), before);
    EXPECT_FALSE(m_document.IsDirty());
}

TEST_F(ScenePlayTest, PauseAndStep)
{
    auto session = Start();
    ASSERT_NE(session, nullptr);
    session->SetPaused(true);
    session->Advance(1s);
    EXPECT_EQ(session->StepIndex(), 0u);
    session->StepOnce();
    EXPECT_EQ(session->StepIndex(), 1u);
    session->SetPaused(false);
    session->Advance(17ms);
    EXPECT_EQ(session->StepIndex(), 2u);
}

TEST_F(ScenePlayTest, EndPlay_KeepsOrRevertsEditsMadeDuringPlay)
{
    for (const bool keep : {true, false})
    {
        auto session = Start();
        ASSERT_NE(session, nullptr);
        const std::size_t mark = m_document.Undo().Applied();
        session->StepOnce();
        ASSERT_TRUE(EditValue(m_project->Data(), m_document.Undo(), "com.test.a:JumpHeight", 3.0f).has_value());
        (void)m_document.Add([](EditorScene& scene) { return scene.CreateObject("added during play"); });
        const std::size_t objects = m_document.Scene().Size();
        EXPECT_EQ(EndPlay(*session, keep, &m_project->Data(), m_document.Undo(), mark), keep ? 1u : 0u);
        // Kept: the chain's 4 overrides the panel edit; reverted: back to the module default, object gone.
        EXPECT_EQ(m_project->Data().Values().Get<float>(kJump), keep ? 4.0f : 2.5f);
        EXPECT_EQ(m_document.Scene().Size(), keep ? objects : objects - 1);
        while (m_document.Undo().Undo())
        {
        }
    }
}

TEST_F(ScenePlayTest, KeepRuntimeValues_IsUndoable)
{
    auto session = Start();
    ASSERT_NE(session, nullptr);
    session->StepOnce();
    EXPECT_EQ(m_project->Data().Values().Get<float>(kJump), 2.5f); // play never writes the project
    EXPECT_EQ(KeepRuntimeValues(session->Runtime().Executor().Values(), m_project->Data(), m_document.Undo()), 1u);
    EXPECT_EQ(m_project->Data().Values().Get<float>(kJump), 4.0f);
    ASSERT_TRUE(m_document.Undo().Undo());
    EXPECT_EQ(m_project->Data().Values().Get<float>(kJump), 2.5f);
}

TEST_F(ScenePlayTest, Debugger_NodeBreakpoint_PausesAfterTheStep_AndStepContinues)
{
    auto session = Start(true);
    ASSERT_NE(session, nullptr);
    ChainDebugger debugger;
    debugger.SetTarget("chains/mover.chain.json");
    debugger.ToggleNodeBreakpoint("move");
    session->SetStepObserver([&](const ScenePlaySession& play)
                             { return debugger.Observe(play.Runtime()).has_value(); });
    session->Advance(101ms); // would be six steps
    EXPECT_TRUE(session->Paused());
    EXPECT_EQ(session->StepIndex(), 1u); // paused after the first step, never mid-wave
    EXPECT_NE(debugger.LastHit().find("move"), std::string::npos);
    EXPECT_FLOAT_EQ(X(*session, m_mover), 1.0f);
    EXPECT_GT(debugger.NodeHeat("move", session->StepIndex()), 0.9f);
    EXPECT_EQ(debugger.NodeHeat("jump", session->StepIndex()) > 0.0f, true); // Start ran in step 0
    session->StepOnce();
    EXPECT_EQ(session->StepIndex(), 2u);
    EXPECT_FALSE(debugger.Timeline().empty());
    const auto watches = [&]
    {
        debugger.AddWatch(std::string(kJump));
        debugger.AddWatch("nothing");
        return debugger.ReadWatches(session->Runtime().Executor().Values());
    }();
    ASSERT_EQ(watches.size(), 2u);
    EXPECT_TRUE(watches[0].value.has_value());
    EXPECT_FALSE(watches[1].value.has_value());
}

TEST_F(ScenePlayTest, Debugger_OtherChainsAreIgnored_WhenTargeted)
{
    auto session = Start(true);
    ASSERT_NE(session, nullptr);
    ChainDebugger debugger;
    debugger.SetTarget("chains/other.chain.json");
    debugger.ToggleNodeBreakpoint("move");
    session->SetStepObserver([&](const ScenePlaySession& play)
                             { return debugger.Observe(play.Runtime()).has_value(); });
    session->Advance(34ms);
    EXPECT_FALSE(session->Paused());
    EXPECT_TRUE(debugger.Timeline().empty());
}
