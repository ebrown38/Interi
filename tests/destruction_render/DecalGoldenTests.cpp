// SPDX-License-Identifier: MIT
// Golden image of crack decals from weakened bonds and a scorch decal on a wall, rendered on lavapipe.
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_render/CrackDecals.h>
#include <interi/render/Camera.h>
#include <interi/render/Decals.h>
#include <interi/render/Mesh.h>
#include <interi/rhi/ResourceManager.h>

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

TEST_F(VulkanTest, Decals_CracksAndScorch_MatchesGolden)
{
    // The bonds within 1.2 m of the wall's centre lose 80% of their health without breaking: the wall still draws
    // as one mesh, with crack lines where those bonds' faces meet its surface, around a scorch mark.
    DestructionScene scene;
    const auto wall = MakeFracturedWall(scene.jobs);
    const DestructibleId id = scene.Spawn(wall);
    scene.Step();
    const Vec3 centre{0.0f, 1.5f, 0.0f};
    for (std::uint32_t bond = 0; bond < wall->bonds.size(); ++bond)
    {
        if (Length(wall->bonds[bond].centroid - centre) < 1.2f)
        {
            scene.destruction.QueueDamage({.center = wall->bonds[bond].centroid,
                                           .energy = scene.destruction.BondFullHealth(id)[bond] * 0.8f,
                                           .innerRadius = 0.001f,
                                           .radius = 0.002f},
                                          DamageKind::Melee);
        }
    }
    scene.Step();

    std::vector<render::Decal> decals;
    GatherCrackDecals(scene.world, scene.destruction, decals);
    ASSERT_GT(decals.size(), 3u);
    decals.push_back({.transform = render::DecalBox({0.2f, 1.4f, 0.15f}, {0.0f, 0.0f, 1.0f}, 1.6f, 1.6f),
                      .color = {0.02f, 0.02f, 0.02f},
                      .intensity = 0.85f,
                      .kind = render::DecalKind::Scorch,
                      .seed = 5});

    render::SceneView view;
    view.cameraPosition = {1.0f, 1.9f, 4.2f};
    view.view = render::LookAt(view.cameraPosition, {0.0f, 1.4f, 0.0f}, {0, 1, 0});
    view.sun = {.direction = {-0.3f, -0.8f, -1.0f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};
    view.ambient = {0.08f, 0.08f, 0.09f};

    rhi::ResourceManager resources(*device);
    auto renderer = render::DecalRenderer::Create(*device, resources, rhi::Format::R8G8B8A8Srgb, 256, 1);
    ASSERT_TRUE(renderer.has_value()) << renderer.error().message;
    ChunkMaterials materials;
    materials.surface.push_back({.baseColor = {0.7f, 0.7f, 0.67f}, .roughness = 0.85f});
    const Image image = interi::testing::RenderSceneWith(
        *device, 128, view,
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
            EXPECT_EQ(stats.chunks, 1u); // nothing broke
        },
        {},
        [&](render::FrameGraph& graph, const render::ForwardOutputs& outputs, render::FGTexture& color)
        {
            const render::DecalView decalView{
                .viewProjection = render::PerspectiveReverseZ(view.verticalFov, 1.0f, view.nearPlane) * view.view};
            (*renderer)->AddPasses(graph, decals, decalView, color, outputs.depth, 128, 128);
        });
    interi::testing::ExpectMatchesGolden(image, "decals_cracks_scorch");
}
