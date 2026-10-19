// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/Primitives.h>
#include <interi/scene/SceneRuntime.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace interi;
using namespace interi::scene;

namespace
{

constexpr std::string_view kMove = R"json({"version": 1, "nodes": [
  {"id": "tick", "event": "trigger.Tick"},
  {"id": "move", "event": "world.Move", "params": {"delta": {"const": [1.0, 0.0, 0.0]}}}],
  "links": [{"from": "tick", "to": "move"}]})json";

constexpr std::string_view kSpawn = R"json({"version": 1, "nodes": [
  {"id": "start", "event": "trigger.Start"},
  {"id": "spawn", "event": "world.SpawnPrefab",
   "params": {"prefab": {"const": "prefabs/crate.prefab.json"}, "position": {"const": [0.0, 5.0, 0.0]}}}],
  "links": [{"from": "start", "to": "spawn"}]})json";

constexpr std::string_view kLiftSpawned = R"json({"version": 1, "nodes": [
  {"id": "spawned", "event": "trigger.Spawned"},
  {"id": "up", "event": "world.Move",
   "params": {"target": {"payload": "entity"}, "delta": {"const": [0.0, 1.0, 0.0]}}}],
  "links": [{"from": "spawned", "to": "up"}]})json";

constexpr std::string_view kCrate = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Name": {"name": "Crate"}, "interi.Primitive": {"shape": "Box"}}},
  {"id": 2, "components": {"interi.Parent": {"parent": 1},
                           "interi.LocalTransform": {"position": [0.0, 1.0, 0.0]}}}]})json";

// Object 1 moves along +x every step and carries child 2; object 3 runs the spawn chains.
constexpr std::string_view kScene = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Chains": {"chains": ["chains/move.chain.json"]}}},
  {"id": 2, "components": {"interi.Parent": {"parent": 1},
                           "interi.LocalTransform": {"position": [0.0, 2.0, 0.0]}}},
  {"id": 3, "components": {"interi.Chains": {"chains": ["chains/spawn.chain.json", "chains/lift.chain.json",
                                                         "chains/missing.chain.json"],
                                              "global": true}}}]})json";

class SceneRuntimeTest : public ::testing::Test
{
protected:
    SceneRuntimeTest()
    {
        for (std::string& id : chain::CoreContracts())
        {
            m_modules.DeclareContract(std::move(id));
        }
        m_chains = std::make_unique<chain::ChainRegistry>(m_modules);
        EXPECT_TRUE(chain::RegisterCoreLibrary(*m_chains).has_value());
        RegisterSceneReflection(m_reflection);
        RegisterPrimitiveReflection(m_reflection);
        RegisterPrefabReflection(m_reflection);
        RegisterChainsReflection(m_reflection);
        Write("chains/move.chain.json", kMove);
        Write("chains/spawn.chain.json", kSpawn);
        Write("chains/lift.chain.json", kLiftSpawned);
        Write("prefabs/crate.prefab.json", kCrate);
    }

    void Write(std::string_view path, std::string_view text)
    {
        ASSERT_TRUE(m_files.WriteFile(path, std::as_bytes(std::span(text))).has_value());
    }

    std::unique_ptr<SceneRuntime> Run(JobSystem& jobs, int steps)
    {
        auto runtime = SceneRuntime::Create(*ParseScene(kScene), m_reflection, *m_chains, nullptr, m_files, jobs);
        EXPECT_TRUE(runtime.has_value()) << runtime.error().message;
        for (int i = 0; i < steps; ++i)
        {
            (*runtime)->Step();
        }
        return std::move(*runtime);
    }

    static Vec3 WorldOf(SceneRuntime& runtime, ObjectId id)
    {
        World& world = runtime.GetWorld();
        for (const auto& [object, node] : BuildHierarchy(world).nodes)
        {
            if (object == id)
            {
                return world.Get<Transform>(node.entity)->position;
            }
        }
        return Vec3(-999.0f);
    }

    sdk::ModuleSet m_modules;
    std::unique_ptr<chain::ChainRegistry> m_chains;
    values::ReflectionRegistry m_reflection;
    MemoryFileSystem m_files;
};

} // namespace

TEST_F(SceneRuntimeTest, ChainsMoveObjectsAndChildrenFollow)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 2, .pinThreads = false});
    auto runtime = Run(jobs, 3);
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, 1), Vec3(3, 0, 0), 0.0f);
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, 2), Vec3(3, 2, 0), 0.0f);
    EXPECT_EQ(runtime->StepIndex(), 3u);
    ASSERT_EQ(runtime->Warnings().size(), 1u); // the missing chain
    EXPECT_NE(runtime->Warnings()[0].find("missing.chain.json"), std::string::npos);
}

TEST_F(SceneRuntimeTest, SpawnPrefab_ExpandsAndRaisesSpawned)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 2, .pinThreads = false});
    auto runtime = Run(jobs, 1);
    const ObjectId spawned = chain::ExecutorDesc{}.firstSpawnId;
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, spawned), Vec3(0, 5, 0), 0.0f);
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, PrefabObjectId(spawned, 2)), Vec3(0, 6, 0), 0.0f);
    runtime->Step(); // trigger.Spawned lifts it by 1
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, spawned), Vec3(0, 6, 0), 0.0f);
    INTERI_EXPECT_VEC_NEAR(WorldOf(*runtime, PrefabObjectId(spawned, 2)), Vec3(0, 7, 0), 0.0f);
}

TEST_F(SceneRuntimeTest, SameScene_IdenticalAt1_4_32Workers)
{
    std::string reference;
    for (const std::uint32_t workers : {1u, 4u, 32u})
    {
        JobSystem jobs(JobSystemDesc{.workerCount = workers, .pinThreads = false});
        auto runtime = Run(jobs, 20);
        const std::string state = WriteScene(CaptureScene(runtime->GetWorld(), m_reflection));
        if (reference.empty())
        {
            reference = state;
        }
        EXPECT_EQ(state, reference) << workers << " workers";
    }
}

TEST_F(SceneRuntimeTest, BadScene_Fails)
{
    JobSystem jobs(JobSystemDesc{.workerCount = 1, .pinThreads = false});
    auto document =
        ParseScene(R"({"version": 2, "objects": [{"id": 1, "components": {"interi.Chains": {"global": 3}}}]})");
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(SceneRuntime::Create(*document, m_reflection, *m_chains, nullptr, m_files, jobs).has_value());
}

TEST_F(SceneRuntimeTest, ChainFunctions_LoadFromProject_AreInlinedAndRun)
{
    sdk::Provider provider;
    provider.id = "com.test.fn";
    provider.rank = 1;
    m_modules.AddProvider(provider);
    Write("content/functions/Lift.chainfn.json", R"json({"version": 1, "name": "Lift",
      "inputs": [{"name": "dy", "type": "float"}],
      "outputs": [{"name": "Done", "payload": []}],
      "nodes": [
        {"id": "in", "event": "com.test.fn:Lift.Entry"},
        {"id": "move", "event": "world.Move", "params": {"delta": {"expr": "vec3(0.0, payload.dy, 0.0)"}}},
        {"id": "out", "event": "com.test.fn:Lift.Return.Done"}],
      "links": [{"from": "in", "to": "move"}, {"from": "move", "to": "out"}]})json");
    Write("chains/move.chain.json", R"json({"version": 1, "nodes": [
      {"id": "tick", "event": "trigger.Tick"},
      {"id": "lift", "event": "com.test.fn:Lift", "params": {"dy": {"const": 2.0}}}],
      "links": [{"from": "tick", "to": "lift"}]})json");
    JobSystem jobs(JobSystemDesc{.workerCount = 2, .pinThreads = false});
    SceneRuntimeDesc desc;
    desc.functionProvider = "com.test.fn";
    auto runtime = SceneRuntime::Create(*ParseScene(kScene), m_reflection, *m_chains, nullptr, m_files, jobs, desc);
    ASSERT_TRUE(runtime.has_value()) << runtime.error().message;
    for (int i = 0; i < 3; ++i)
    {
        (*runtime)->Step();
    }
    INTERI_EXPECT_VEC_NEAR(WorldOf(**runtime, 1), Vec3(0, 6, 0), 0.0f);
}
