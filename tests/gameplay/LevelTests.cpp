// SPDX-License-Identifier: MIT
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/gameplay/Level.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ExpectNear.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <numbers>
#include <string>

using interi::Vec3;
using interi::gameplay::LoadLevel;
using interi::gameplay::ParseLevel;

namespace
{

constexpr const char* kLevel = R"({
  "staticBoxes": [ { "position": [0, -0.5, 0], "halfExtents": [50, 0.5, 50] } ],
  "destructibles": [
    { "asset": "assets/wall.idest", "position": [0, 0, -5] },
    { "asset": "assets/wall.idest", "position": [6, 0, -5], "rotation": [0, 0.7071068, 0, 0.7071068] },
    { "asset": "assets/tower.idest", "position": [-6, 0, -5] }
  ],
  "spawnPoints": [ { "name": "player", "position": [0, 0, 8], "yawDegrees": 90 }, { "name": "alt", "position": [1, 2, 3] } ]
})";

void WriteAsset(interi::MemoryFileSystem& files, const std::string& path, int width, int height)
{
    const auto asset =
        interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(width, height, 1));
    const std::vector<std::byte> bytes = interi::destruction::WriteDestructibleAsset(asset, 1);
    ASSERT_TRUE(files.WriteFile(path, bytes).has_value());
}

} // namespace

TEST(Level, Parse_ReadsEverything)
{
    const auto level = ParseLevel(kLevel);
    ASSERT_TRUE(level.has_value()) << level.error().message;
    EXPECT_TRUE(level->warnings.empty());
    ASSERT_EQ(level->staticBoxes.size(), 1u);
    INTERI_EXPECT_VEC_NEAR(level->staticBoxes[0].halfExtents, Vec3(50.0f, 0.5f, 50.0f), 0.0f);
    ASSERT_EQ(level->destructibles.size(), 3u);
    EXPECT_EQ(level->destructibles[1].asset, "assets/wall.idest");
    EXPECT_NEAR(level->destructibles[1].transform.rotation.y, 0.7071068f, 1e-6f);
    EXPECT_EQ(level->destructibles[0].transform.rotation.w, 1.0f);
    ASSERT_EQ(level->spawnPoints.size(), 2u);
    EXPECT_NEAR(level->spawnPoints[0].yaw, std::numbers::pi_v<float> / 2.0f, 1e-6f);
}

TEST(Level, Invalid_Fails)
{
    const char* const cases[] = {
        "[]",
        R"({ "staticBoxes": [ { "position": [0, 0, 0] } ] })",                    // no halfExtents
        R"({ "staticBoxes": [ { "halfExtents": [1, 0, 1] } ] })",                 // flat box
        R"({ "destructibles": [ { "position": [0, 0, 0] } ] })",                  // no asset
        R"({ "destructibles": [ { "asset": "a.idest", "position": [0, 0] } ] })", // 2 numbers
        R"({ "destructibles": [ { "asset": "a.idest", "position": [0, 0, 0], "rotation": [0, 0, 0, 0] } ] })",
        R"({ "spawnPoints": [ { "position": [0, 0, 0] } ] })", // no name
        R"({ "spawnPoints": {} })",
    };
    for (const char* json : cases)
    {
        const auto level = ParseLevel(json);
        ASSERT_FALSE(level.has_value()) << json;
        EXPECT_EQ(level.error().code, interi::ErrorCode::InvalidArgument) << json;
    }
}

TEST(Level, UnknownField_Warns)
{
    const auto level =
        ParseLevel(R"({ "music": "x", "spawnPoints": [ { "name": "a", "position": [0,0,0], "team": 1 } ] })");
    ASSERT_TRUE(level.has_value());
    EXPECT_EQ(level->warnings.size(), 2u);
}

TEST(Level, Load_SpawnsDestructibles)
{
    interi::MemoryFileSystem files;
    WriteAsset(files, "assets/wall.idest", 4, 3);
    WriteAsset(files, "assets/tower.idest", 1, 5);
    ASSERT_TRUE(files.WriteFile("levels/test.json", std::as_bytes(std::span(std::string_view(kLevel)))).has_value());

    interi::testing::GameplayScene scene;
    interi::destruction::DestructionWorld destruction(scene.physics);
    const auto def = LoadLevel(files, "levels/test.json");
    ASSERT_TRUE(def.has_value()) << def.error().message;
    auto level = interi::gameplay::SpawnLevel(*def, files, scene.world, scene.jobs, destruction);
    ASSERT_TRUE(level.has_value()) << level.error().message;

    ASSERT_EQ(level->destructibles.size(), 3u);
    EXPECT_EQ(destruction.InstanceCount(), 3u);
    // Both walls share one asset (read once); the tower has its own.
    EXPECT_EQ(&destruction.Asset(level->destructibles[0]), &destruction.Asset(level->destructibles[1]));
    EXPECT_EQ(destruction.Asset(level->destructibles[2]).chunks.size(), 5u);
    INTERI_EXPECT_VEC_NEAR(destruction.InstanceTransform(level->destructibles[1]).position, Vec3(6.0f, 0.0f, -5.0f),
                           0.0f);
    ASSERT_EQ(level->staticEntities.size(), 1u);
    EXPECT_TRUE(scene.world.Has<interi::physics::ColliderComponent>(level->staticEntities[0]));

    EXPECT_EQ(level->FindSpawnPoint("alt")->position.y, 2.0f);
    EXPECT_EQ(level->FindSpawnPoint("nope")->name, "player"); // falls back to the first

    // Simulates: the walls stand on the ground box.
    for (int i = 0; i < 30; ++i)
    {
        scene.Step();
        destruction.Update(scene.world, scene.jobs);
    }
    EXPECT_EQ(destruction.LastStats().bondsBrokenByStress, 0u);

    interi::gameplay::DespawnLevel(*level, scene.world, destruction);
    EXPECT_EQ(destruction.InstanceCount(), 0u);
    EXPECT_TRUE(level->destructibles.empty());
}

TEST(Level, MissingAsset_CreatesNothing)
{
    interi::MemoryFileSystem files;
    WriteAsset(files, "assets/wall.idest", 4, 3);
    const auto def = ParseLevel(kLevel); // tower is missing
    ASSERT_TRUE(def.has_value());
    interi::testing::GameplayScene scene;
    interi::destruction::DestructionWorld destruction(scene.physics);
    const auto level = interi::gameplay::SpawnLevel(*def, files, scene.world, scene.jobs, destruction);
    ASSERT_FALSE(level.has_value());
    EXPECT_EQ(destruction.InstanceCount(), 0u);
    EXPECT_EQ(scene.world.EntityCount(), 0u);
}

TEST(Level, AssetCache_ReusedAcrossLoads)
{
    interi::MemoryFileSystem files;
    WriteAsset(files, "assets/wall.idest", 4, 3);
    WriteAsset(files, "assets/tower.idest", 1, 5);
    const auto def = ParseLevel(kLevel);
    ASSERT_TRUE(def.has_value());
    interi::testing::GameplayScene scene;
    interi::destruction::DestructionWorld destruction(scene.physics);
    interi::gameplay::AssetCache cache;
    auto first = interi::gameplay::SpawnLevel(*def, files, scene.world, scene.jobs, destruction, &cache);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->assets.size(), 2u);
    EXPECT_EQ(cache.size(), 2u);
    interi::gameplay::DespawnLevel(*first, scene.world, destruction);

    interi::MemoryFileSystem empty; // everything comes from the cache now
    auto second = interi::gameplay::SpawnLevel(*def, empty, scene.world, scene.jobs, destruction, &cache);
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(second->assets, (std::vector{cache.at("assets/wall.idest"), cache.at("assets/tower.idest")}));
}
