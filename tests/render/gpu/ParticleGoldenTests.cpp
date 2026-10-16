// SPDX-License-Identifier: MIT
// Golden image of GPU particles drawn over a forward-rendered scene, on lavapipe.
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/Particles.h>
#include <interi/rhi/ResourceManager.h>

#include "GoldenImage.h"
#include "RenderScene.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::Image;
using interi::testing::SceneContext;
using interi::testing::VulkanTest;

TEST_F(VulkanTest, Particles_DustAndSparks_MatchesGolden)
{
    // A ring of grey dust puffs (alpha) around a cube, half of them behind it, and a fan of orange sparks
    // (additive) in front. The cube hides the puffs behind it; puffs touching the ground fade into it (soft).
    SceneView view;
    view.cameraPosition = {0.0f, 1.6f, 4.0f};
    view.view = LookAt(view.cameraPosition, {0.0f, 0.5f, 0.0f}, {0, 1, 0});
    view.sun = {.direction = {-0.4f, -1.0f, -0.6f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};

    std::vector<Particle> particles;
    for (int i = 0; i < 12; ++i)
    {
        const float angle = static_cast<float>(i) * 0.5236f;
        Particle dust;
        dust.position = {std::cos(angle) * 1.1f, 0.25f + 0.05f * static_cast<float>(i % 3), std::sin(angle) * 1.1f};
        dust.life = 1.0f;
        dust.lifetime = 1.0f;
        dust.size = 0.7f;
        dust.color = PackColor(110, 95, 80, 220);
        particles.push_back(dust);
    }
    for (int i = 0; i < 16; ++i)
    {
        Particle spark;
        spark.position = {-0.8f + 0.1f * static_cast<float>(i), 1.2f + 0.02f * static_cast<float>(i % 5), 0.9f};
        spark.life = 0.5f;
        spark.lifetime = 1.0f; // half faded
        spark.size = 0.08f;
        spark.color = PackColor(255, 160, 60, 255);
        spark.flags = kParticleAdditive;
        particles.push_back(spark);
    }

    rhi::ResourceManager resources(*device);
    auto system = GpuParticleSystem::Create(*device, resources, rhi::Format::R8G8B8A8Srgb,
                                            {.capacity = 256, .maxEmitPerFrame = 64, .framesInFlight = 1});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    ASSERT_EQ((*system)->Emit(particles), particles.size());

    const Image image = interi::testing::RenderSceneWith(
        *device, 128, view,
        [&](SceneContext& context, std::vector<RenderObject>& objects)
        {
            auto cube = context.meshes.Add(context.uploads, context.commands, MakeCube(0.5f));
            auto ground = context.meshes.Add(context.uploads, context.commands, MakePlane(4.0f));
            ASSERT_TRUE(cube && ground);
            objects.push_back({.mesh = *cube,
                               .transform = Mat4::Translation({0.0f, 0.5f, 0.0f}),
                               .material = {.baseColor = {0.3f, 0.45f, 0.7f}}});
            objects.push_back({.mesh = *ground, .material = {.baseColor = {0.45f, 0.45f, 0.4f}}, .castsShadow = false});
        },
        {},
        [&](FrameGraph& graph, const ForwardOutputs& outputs, FGTexture& color)
        {
            const ParticleView particleView{.viewProjection =
                                                PerspectiveReverseZ(view.verticalFov, 1.0f, view.nearPlane) * view.view,
                                            .view = view.view,
                                            .nearPlane = view.nearPlane};
            (*system)->AddPasses(graph, 0.0f, particleView, color, outputs.depth, 128, 128);
        });
    interi::testing::ExpectMatchesGolden(image, "particles_dust_sparks");
}
