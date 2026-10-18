// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneSave.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ValueRegistry.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::scene;

namespace
{

constexpr std::string_view kMove = R"json({"version": 1, "nodes": [
  {"id": "tick", "event": "trigger.Tick"},
  {"id": "move", "event": "world.Move", "params": {"delta": {"const": [1.0, 0.0, 0.0]}}},
  {"id": "score", "event": "world.ModifyValue", "params": {"id": {"const": "com.test:Score"}, "delta": {"const": 2}}},
  {"id": "hide", "event": "world.Hide", "params": {"target": {"const": 3}}}],
  "links": [{"from": "tick", "to": "move"}, {"from": "move", "to": "score"}, {"from": "score", "to": "hide"}]})json";

// 1 moves every step (and scores); 2 is a coin that vanishes; 3 gets hidden.
constexpr std::string_view kScene = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Chains": {"chains": ["chains/move.chain.json"]}}},
  {"id": 2, "components": {"interi.LocalTransform": {"position": [5.0, 0.0, 0.0]}}},
  {"id": 3, "components": {"interi.LocalTransform": {"position": [0.0, 7.0, 0.0]}}}]})json";

class SceneSaveTest : public ::testing::Test
{
protected:
    SceneSaveTest()
        : m_values(m_modules)
        , m_chains(m_modules)
    {
        sdk::Provider provider;
        provider.id = "com.test";
        provider.rank = 1;
        m_modules.AddProvider(provider);
        for (std::string& id : chain::CoreContracts())
        {
            m_modules.DeclareContract(std::move(id));
        }
        EXPECT_TRUE(chain::RegisterCoreLibrary(m_chains).has_value());
        values::ValueDefinition score;
        score.id = "com.test:Score";
        score.schema.type = values::ValueType::Int;
        score.schema.defaultValue = std::int32_t{0};
        EXPECT_TRUE(m_values.Add("com.test", score).has_value());
        RegisterSceneReflection(m_reflection);
        RegisterChainsReflection(m_reflection);
        const std::string_view move = kMove;
        EXPECT_TRUE(m_files.WriteFile("chains/move.chain.json", std::as_bytes(std::span(move))).has_value());
    }

    std::unique_ptr<SceneRuntime> Create()
    {
        auto runtime = SceneRuntime::Create(*ParseScene(kScene), m_reflection, m_chains, &m_values, m_files, m_jobs);
        EXPECT_TRUE(runtime.has_value()) << runtime.error().message;
        return std::move(*runtime);
    }

    Vec3 Local(SceneRuntime& runtime, ObjectId id)
    {
        World& world = runtime.GetWorld();
        for (const auto& [object, node] : BuildHierarchy(world).nodes)
        {
            if (object == id)
            {
                return world.Get<LocalTransformComponent>(node.entity)->position;
            }
        }
        return Vec3(-999.0f);
    }

    bool Exists(SceneRuntime& runtime, ObjectId id) { return BuildHierarchy(runtime.GetWorld()).nodes.contains(id); }

    sdk::ModuleSet m_modules;
    values::ValueRegistry m_values;
    chain::ChainRegistry m_chains;
    values::ReflectionRegistry m_reflection;
    MemoryFileSystem m_files;
    JobSystem m_jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
};

} // namespace

TEST_F(SceneSaveTest, CaptureRestore_IntoFreshRuntime_ReproducesPlacementValuesAndHiding)
{
    auto original = Create();
    for (int i = 0; i < 3; ++i)
    {
        original->Step();
    }
    ASSERT_TRUE(original->GetWorld().DestroyEntity(BuildHierarchy(original->GetWorld()).nodes.at(2).entity));
    const std::vector<std::byte> bytes = CaptureSceneState(*original);

    auto fresh = Create();
    ASSERT_TRUE(RestoreSceneState(*fresh, bytes, kSceneSaveVersion).has_value());
    EXPECT_EQ(Local(*fresh, 1), Vec3(3.0f, 0.0f, 0.0f));
    EXPECT_FALSE(Exists(*fresh, 2)); // the coin is gone
    EXPECT_TRUE(Exists(*fresh, 3));
    const World& world = fresh->GetWorld();
    EXPECT_TRUE(world.Has<chain::HiddenComponent>(BuildHierarchy(fresh->GetWorld()).nodes.at(3).entity));
    const values::Value* score = fresh->Executor().Values().Read("com.test:Score");
    ASSERT_NE(score, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(score->Variant()), 6);
    // World transforms follow (propagated).
    EXPECT_EQ(fresh->GetWorld().Get<Transform>(BuildHierarchy(fresh->GetWorld()).nodes.at(1).entity)->position,
              Vec3(3.0f, 0.0f, 0.0f));
}

TEST_F(SceneSaveTest, Restore_UnhidesAndMovesBack)
{
    auto runtime = Create();
    const std::vector<std::byte> start = CaptureSceneState(*runtime);
    for (int i = 0; i < 4; ++i)
    {
        runtime->Step();
    }
    ASSERT_NE(Local(*runtime, 1), Vec3(0.0f, 0.0f, 0.0f));
    ASSERT_TRUE(RestoreSceneState(*runtime, start, kSceneSaveVersion).has_value());
    EXPECT_EQ(Local(*runtime, 1), Vec3(0.0f));
    EXPECT_FALSE(
        runtime->GetWorld().Has<chain::HiddenComponent>(BuildHierarchy(runtime->GetWorld()).nodes.at(3).entity));
    EXPECT_EQ(runtime->Executor().Values().Overrides().size(), 0u);
}

TEST_F(SceneSaveTest, Restore_CorruptBytes_Fails)
{
    auto runtime = Create();
    std::vector<std::byte> bytes = CaptureSceneState(*runtime);
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(RestoreSceneState(*runtime, bytes, kSceneSaveVersion).has_value());
    EXPECT_FALSE(RestoreSceneState(*runtime, {}, kSceneSaveVersion).has_value());
    EXPECT_TRUE(Exists(*runtime, 2)); // nothing destroyed by the failed restore
}

TEST_F(SceneSaveTest, SaveRegistry_RoundTripsThroughAFile)
{
    auto original = Create();
    for (int i = 0; i < 2; ++i)
    {
        original->Step();
    }
    SaveRegistry registry;
    ASSERT_TRUE(RegisterSceneSaveSection(registry, *original).has_value());
    SaveSlots slots(m_files, "saves", 3);
    ASSERT_TRUE(slots.Write(0, registry.Capture("two steps")).has_value());

    auto fresh = Create();
    SaveRegistry freshRegistry;
    ASSERT_TRUE(RegisterSceneSaveSection(freshRegistry, *fresh).has_value());
    const auto game = slots.Read(0);
    ASSERT_TRUE(game.has_value());
    const auto report = freshRegistry.Apply(*game);
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->failed.empty());
    EXPECT_EQ(Local(*fresh, 1), Vec3(2.0f, 0.0f, 0.0f));
}

TEST_F(SceneSaveTest, CaptureIsDeterministic)
{
    auto a = Create();
    auto b = Create();
    for (int i = 0; i < 3; ++i)
    {
        a->Step();
        b->Step();
    }
    EXPECT_EQ(CaptureSceneState(*a), CaptureSceneState(*b));
}
