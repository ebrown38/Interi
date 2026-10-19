// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Picking.h>
#include <interi/editor/Reflection.h>
#include <interi/testing/ExpectNear.h>
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
    scene::PrimitiveParams box;
    box.size = Vec3(2.0f);
    const EditorEntityId far = scene.CreatePrimitive(box, At({0, 1, -10}));
    const EditorEntityId near = scene.CreatePrimitive(box, At({0, 1, -4}));
    const EditorEntityId wall = scene.CreateObject("wall", At({5, 0, 0}));
    ASSERT_TRUE(scene.AddComponent(wall, scene::MeshComponent::kComponentName));
    scene.GetWorld().GetMut<scene::MeshComponent>(*scene.Find(wall))->asset.path = "wall.imesh";
    const AssetBoundsFn bounds = [](std::string_view asset) -> std::optional<AABB>
    {
        if (asset == "wall.imesh")
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

    EXPECT_FALSE(PickEntity(scene, {.origin = {0, 10, 10}, .direction = {0, 0, -1}}, bounds).has_value());
    (void)far;
}

TEST(Picking, ChildAndScaledPrimitive_UseWorldBounds)
{
    ReflectionRegistry reflection;
    RegisterEngineReflection(reflection);
    EditorScene scene(reflection);
    Transform group = At({10, 0, 0});
    group.scale = Vec3(3.0f);
    const EditorEntityId parent = scene.CreateObject("group", group);
    scene::PrimitiveParams sphere;
    sphere.shape = scene::PrimitiveShape::Sphere;
    const EditorEntityId ball = scene.CreatePrimitive(sphere, At({0, 1, 0}), "", parent);
    EXPECT_FALSE(PickBoxOf(scene, parent, {}).has_value()); // a group has no shape of its own
    const auto box = PickBoxOf(scene, ball, {});
    ASSERT_TRUE(box.has_value());
    INTERI_EXPECT_VEC_NEAR(box->centre, Vec3(10, 3, 0), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(box->halfExtents, Vec3(1.5f), 1e-6f);
    const auto hit = PickEntity(scene, {.origin = {10, 4.4f, 10}, .direction = {0, 0, -1}}, {});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->id, ball);
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
    touch("levels/a.scene.json");
    touch("meshes/csg.imesh");
    touch("prefabs/crate.prefab.json");
    touch("walls/building.gltf");
    touch("walls/building.idest");
    touch("gameplay/weapons.json");
    touch("audio/clip.wav");

    const auto assets = ListAssets(dir.Path());
    ASSERT_EQ(assets.size(), 3u) << "only scenes, meshes and prefabs: modules list their own kinds";
    EXPECT_EQ(assets[0].path, "levels/a.scene.json");
    EXPECT_EQ(assets[0].kind, AssetKind::Scene);
    EXPECT_EQ(assets[1].kind, AssetKind::Mesh);
    EXPECT_EQ(ToString(AssetKind::Mesh), "mesh");
    EXPECT_EQ(assets[2].kind, AssetKind::Prefab);
    EXPECT_TRUE(ListAssets(dir.Path() / "missing").empty());
}
