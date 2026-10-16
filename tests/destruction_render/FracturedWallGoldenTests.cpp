// SPDX-License-Identifier: MIT
// Golden images of destructibles drawn through ChunkGatherer, rendered on lavapipe.
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/render/Camera.h>
#include <interi/render/Mesh.h>

#include "../render/gpu/GoldenImage.h"
#include "../render/gpu/RenderScene.h"
#include "DestructionRenderTestUtil.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <vector>

using namespace interi;
using namespace interi::destruction;
using interi::destruction::testing::DestructionScene;
using interi::destruction::testing::MakeFracturedWall;
using interi::testing::Image;
using interi::testing::SceneContext;
using interi::testing::VulkanTest;

namespace
{

render::SceneView WallView()
{
    render::SceneView view;
    view.cameraPosition = {5.0f, 4.5f, 4.0f};
    view.view = render::LookAt(view.cameraPosition, {0.0f, 1.0f, 0.0f}, {0, 1, 0});
    view.sun = {.direction = {-0.5f, -1.0f, -0.7f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};
    view.ambient = {0.08f, 0.08f, 0.09f};
    return view;
}

} // namespace

TEST_F(VulkanTest, FracturedWall_MatchesGolden)
{
    // An explosion just behind the wall blows a hole through it and throws the pieces towards the camera. The
    // broken regions draw per chunk (brown fracture faces); the untouched ones stay one mesh each.
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    (void)scene.Spawn(wall);
    scene.Step(2);
    scene.destruction.QueueDamage(
        {.center = {0.6f, 1.8f, -0.4f}, .energy = 1.0e9f, .innerRadius = 0.9f, .radius = 1.4f}, DamageKind::Explosion);
    scene.Step(45);

    ChunkMaterials materials;
    materials.surface.push_back({.baseColor = {0.62f, 0.63f, 0.6f}, .roughness = 0.85f});
    materials.interior.push_back({.baseColor = {0.45f, 0.38f, 0.3f}, .roughness = 0.95f});
    const Image image = interi::testing::RenderSceneWith(
        *device, 128, WallView(),
        [&](SceneContext& context, std::vector<render::RenderObject>& objects)
        {
            auto ground = context.meshes.Add(context.uploads, context.commands, render::MakePlane(6.0f));
            ASSERT_TRUE(ground.has_value());
            objects.push_back({.mesh = *ground,
                               .material = {.baseColor = {0.35f, 0.4f, 0.33f}, .roughness = 0.9f},
                               .castsShadow = false});
            ChunkMeshCache cache(context.meshes);
            ASSERT_TRUE(cache.Add(context.uploads, context.commands, wall).has_value());
            ChunkGatherer gatherer;
            const ChunkGatherStats stats =
                gatherer.Gather(scene.world, scene.destruction, cache, materials, objects, 4096);
            EXPECT_GE(stats.pieces, 2u);
        });
    interi::testing::ExpectMatchesGolden(image, "fractured_wall");
}
