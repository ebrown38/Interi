// SPDX-License-Identifier: MIT
// Golden image tests of the forward renderer, rendered on lavapipe.
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/render/Camera.h>

#include "GoldenImage.h"
#include "RenderScene.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <numbers>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::Image;
using interi::testing::SceneObject;
using interi::testing::VulkanTest;

namespace
{

SceneView MakeView(Vec3 eye)
{
    SceneView view;
    view.cameraPosition = eye;
    view.view = LookAt(eye, {0, 0, 0}, {0, 1, 0});
    view.sun = {.direction = {-0.4f, -1.0f, -0.6f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};
    return view;
}

} // namespace

TEST_F(VulkanTest, Render_LitCube_MatchesGolden)
{
    const SceneObject cube{
        .mesh = 0,
        .object = {.transform = Mat4::Rotation(Quat::FromAxisAngle({0, 1, 0}, -std::numbers::pi_v<float> / 9.0f)),
                   .material = {.baseColor = {0.8f, 0.3f, 0.2f}, .metallic = 0.0f, .roughness = 0.4f}}};
    const Image image =
        interi::testing::RenderScene(*device, 128, MakeView({2.2f, 1.8f, 3.0f}), {MakeCube(0.6f)}, {cube});
    interi::testing::ExpectMatchesGolden(image, "lit_cube");
}

TEST_F(VulkanTest, Render_ShadowsAndPointLights_MatchesGolden)
{
    // A cube on a ground plane: the sun casts its shadow onto the plane, and two coloured point lights light the
    // plane through the clusters.
    const std::vector<PointLight> lights{
        {.position = {-1.6f, 0.4f, 1.2f}, .radius = 3.0f, .color = {0.2f, 0.4f, 1.0f}, .intensity = 6.0f},
        {.position = {1.5f, 0.4f, 1.6f}, .radius = 3.0f, .color = {1.0f, 0.5f, 0.1f}, .intensity = 6.0f},
    };
    SceneView view = MakeView({3.5f, 3.0f, 4.5f});
    view.sun = {.direction = {0.6f, -1.0f, 0.5f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 2.5f};
    view.pointLights = lights;
    const std::vector<SceneObject> objects{
        {.mesh = 0,
         .object = {.transform = Mat4::Translation({0, 0.5f, 0}), .material = {.baseColor = {0.7f, 0.7f, 0.7f}}}},
        {.mesh = 1,
         .object = {.material = {.baseColor = {0.5f, 0.55f, 0.5f}, .roughness = 0.8f}, .castsShadow = false}},
    };
    const Image image = interi::testing::RenderScene(*device, 128, view, {MakeCube(0.5f), MakePlane(4.0f)}, objects,
                                                     ForwardSettings{.shadowMapSize = 512});
    interi::testing::ExpectMatchesGolden(image, "shadows_point_lights");
}
