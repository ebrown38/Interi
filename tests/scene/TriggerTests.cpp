// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/ecs/Query.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneRuntime.h>
#include <interi/scene/Triggers.h>
#include <interi/sdk/ContributionRegistry.h>

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

// Entering a "coin" volume lifts the visitor by 10; leaving moves it 1 along z.
constexpr std::string_view kCoin = R"json({"version": 1, "nodes": [
  {"id": "in", "event": "trigger.Overlap", "params": {"key": {"const": "coin"}}},
  {"id": "lift", "event": "world.Move", "params": {"target": {"payload": "other"}, "delta": {"const": [0.0, 10.0, 0.0]}}},
  {"id": "out", "event": "trigger.OverlapEnd", "params": {"key": {"const": "coin"}}},
  {"id": "shift", "event": "world.Move", "params": {"target": {"payload": "other"}, "delta": {"const": [0.0, 0.0, 1.0]}}}],
  "links": [{"from": "in", "to": "lift"}, {"from": "out", "to": "shift"}]})json";

// 1: the walker (an actor) moving +x per step. 2: a coin volume at x = 3. 3: a volume tagged otherwise (no chain
// reacts). 4: the global chain.
constexpr std::string_view kScene = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Chains": {"chains": ["chains/move.chain.json"]},
                           "interi.TriggerActor": {"radius": 0.0}}},
  {"id": 2, "components": {"interi.LocalTransform": {"position": [3.0, 0.0, 0.0]},
                           "interi.TriggerVolume": {"shape": "Box", "halfExtents": [0.5, 0.5, 0.5], "tag": "coin"}}},
  {"id": 3, "components": {"interi.LocalTransform": {"position": [1.0, 0.0, 0.0]},
                           "interi.TriggerVolume": {"shape": "Sphere", "radius": 0.2, "tag": "other"}}},
  {"id": 4, "components": {"interi.Chains": {"chains": ["chains/coin.chain.json"], "global": true}}}]})json";

class TriggerTest : public ::testing::Test
{
protected:
    TriggerTest()
    {
        for (std::string& id : chain::CoreContracts())
        {
            m_modules.DeclareContract(std::move(id));
        }
        m_chains = std::make_unique<chain::ChainRegistry>(m_modules);
        EXPECT_TRUE(chain::RegisterCoreLibrary(*m_chains).has_value());
        RegisterSceneReflection(m_reflection);
        RegisterChainsReflection(m_reflection);
        RegisterTriggerReflection(m_reflection);
        Write("chains/move.chain.json", kMove);
        Write("chains/coin.chain.json", kCoin);
    }

    void Write(std::string_view path, std::string_view text)
    {
        ASSERT_TRUE(m_files.WriteFile(path, std::as_bytes(std::span(text))).has_value());
    }

    Vec3 Walker(std::uint32_t workers, int steps)
    {
        JobSystem jobs(JobSystemDesc{.workerCount = workers, .pinThreads = false});
        auto runtime = SceneRuntime::Create(*ParseScene(kScene), m_reflection, *m_chains, nullptr, m_files, jobs);
        EXPECT_TRUE(runtime.has_value()) << runtime.error().message;
        for (int i = 0; i < steps; ++i)
        {
            (*runtime)->Step();
        }
        World& world = (*runtime)->GetWorld();
        for (const auto& [id, node] : BuildHierarchy(world).nodes)
        {
            if (id == 1)
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

TEST_F(TriggerTest, EnterAndLeave_RaiseChainTriggersTheNextStep)
{
    // Step 3 puts the walker at x = 3 (inside the coin volume); the enter trigger fires in step 4 (lift, and
    // x = 4 leaves the volume), the leave trigger in step 5.
    EXPECT_EQ(Walker(2, 3), Vec3(3.0f, 0.0f, 0.0f));
    EXPECT_EQ(Walker(2, 4), Vec3(4.0f, 10.0f, 0.0f));
    EXPECT_EQ(Walker(2, 5), Vec3(5.0f, 10.0f, 1.0f));
}

TEST_F(TriggerTest, ResultIsIdenticalAtAnyWorkerCount)
{
    for (const std::uint32_t workers : {0u, 1u, 4u, 16u})
    {
        EXPECT_EQ(Walker(workers, 6), Vec3(6.0f, 10.0f, 1.0f)) << workers;
    }
}

TEST_F(TriggerTest, Reflection_RoundTripsComponents)
{
    EXPECT_NE(m_reflection.Find(TriggerVolumeComponent::kComponentName), nullptr);
    EXPECT_NE(m_reflection.Find(TriggerActorComponent::kComponentName), nullptr);
}

TEST(TriggerScan, IsSortedById_AfterRemovals)
{
    World world;
    for (const ObjectId id : {5u, 3u, 9u, 4u})
    {
        const Entity entity = CreateObject(world, id, "v");
        world.Add(entity, TriggerVolumeComponent{.shape = {"Box"},
                                                 .halfExtents = Vec3(1.0f),
                                                 .radius = 1.0f,
                                                 .tag = "t" + std::to_string(id),
                                                 .mask = ~0u});
        world.Add(entity, TriggerActorComponent{});
    }
    // Removing the first created swaps the last into its slot.
    Query().With<StableIdComponent>().ForEachChunk(world,
                                                   [&](ChunkView& view)
                                                   {
                                                       const auto ids = view.Read<StableIdComponent>();
                                                       for (std::size_t i = 0; i < ids.size(); ++i)
                                                       {
                                                           if (ids[i].id == 5u)
                                                           {
                                                               (void)world.DestroyEntity(view.Entities()[i]);
                                                               return;
                                                           }
                                                       }
                                                   });
    const TriggerScan scan = ScanTriggers(world);
    ASSERT_EQ(scan.volumes.size(), 3u);
    EXPECT_EQ(scan.volumes[0].id, 3u);
    EXPECT_EQ(scan.volumes[1].id, 4u);
    EXPECT_EQ(scan.volumes[2].id, 9u);
    EXPECT_EQ(scan.tags[0], "t3");
    EXPECT_EQ(scan.tags[2], "t9");
    EXPECT_EQ(scan.actors[0].id, 3u);
    EXPECT_EQ(scan.actors[2].id, 9u);
}
