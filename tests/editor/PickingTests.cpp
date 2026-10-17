// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Picking.h>
#include <interi/editor/Reflection.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <fstream>

using namespace interi;
using namespace interi::editor;

namespace
{

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

} // namespace

TEST(Picking, Ray_SelectsNearestEntity)
{
    ReflectionRegistry reflection;
    RegisterEngineReflection(reflection);
    EditorScene scene(reflection);
    const EditorEntityId far = scene.CreateStaticBox(At({0, 1, -10}), Vec3(1.0f));
    const EditorEntityId near = scene.CreateStaticBox(At({0, 1, -4}), Vec3(1.0f));
    const EditorEntityId wall = scene.CreateDestructible(At({5, 0, 0}), "wall.idest");
    const EditorEntityId spawn = scene.CreateSpawnPoint(At({-5, 0, 0}), "player");
    const AssetBoundsFn bounds = [](std::string_view asset) -> std::optional<AABB>
    {
        if (asset == "wall.idest")
        {
            return AABB{.min = {-2, 0, -0.15f}, .max = {2, 3, 0.15f}};
        }
        return std::nullopt;
    };

    const Ray forward{.origin = {0, 1, 10}, .direction = {0, 0, -1}};
    const auto hit = PickEntity(scene, forward, bounds);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id, near);
    EXPECT_NEAR(hit->distance, 13.0f, 1e-4f);

    // The wall's bounds reach 3 m up; its placeholder (no bounds) would not.
    const auto high = PickEntity(scene, {.origin = {5, 2.5f, 10}, .direction = {0, 0, -1}}, bounds);
    ASSERT_TRUE(high.has_value());
    EXPECT_EQ(high->id, wall);
    EXPECT_FALSE(PickEntity(scene, {.origin = {5, 2.5f, 10}, .direction = {0, 0, -1}}, {}).has_value());

    const auto spawnHit = PickEntity(scene, {.origin = {-5, 1.5f, 10}, .direction = {0, 0, -1}}, bounds);
    ASSERT_TRUE(spawnHit.has_value());
    EXPECT_EQ(spawnHit->id, spawn);
    EXPECT_FALSE(PickEntity(scene, {.origin = {0, 10, 10}, .direction = {0, 0, -1}}, bounds).has_value());
    (void)far;
}

TEST(AssetBrowser, ListsAssetsByKind)
{
    const interi::testing::ScopedTempDir dir;
    const auto touch = [&](const std::string& path)
    {
        std::filesystem::create_directories((dir.Path() / path).parent_path());
        std::ofstream(dir.Path() / path) << "x";
    };
    touch("levels/a.json");
    touch("destruction/samples/building.gltf");
    touch("destruction/samples/building.fracture.json");
    touch("destruction/samples/building.idest");
    touch("gameplay/weapons.json");
    touch("audio/clip.wav");

    const auto assets = ListAssets(dir.Path());
    ASSERT_EQ(assets.size(), 3u);
    EXPECT_EQ(assets[0].path, "destruction/samples/building.gltf");
    EXPECT_EQ(assets[0].kind, AssetKind::Source);
    EXPECT_EQ(assets[1].path, "destruction/samples/building.idest");
    EXPECT_EQ(assets[1].kind, AssetKind::Destructible);
    EXPECT_EQ(assets[2].path, "levels/a.json");
    EXPECT_EQ(assets[2].kind, AssetKind::Level);
    EXPECT_TRUE(ListAssets(dir.Path() / "missing").empty());
}

TEST(Picking, RayMesh_NearestTriangle)
{
    const auto box = destruction::MakeBoxMesh({-1, 0, -0.5f}, {1, 2, 0.5f});
    const auto hit = RayMesh({.origin = {0, 1, 10}, .direction = {0, 0, -1}}, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(*hit, 9.5f, 1e-5f);
    const auto inside = RayMesh({.origin = {0, 1, 0}, .direction = {1, 0, 0}}, box); // back faces count too
    ASSERT_TRUE(inside.has_value());
    EXPECT_NEAR(*inside, 1.0f, 1e-5f);
    EXPECT_FALSE(RayMesh({.origin = {0, 5, 10}, .direction = {0, 0, -1}}, box).has_value());
}
