// SPDX-License-Identifier: MIT
// GPU-driven culling: the visible-object counts the culling pass produces, read back from the GPU.
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/Shadows.h>

#include "RenderScene.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::SceneObject;
using interi::testing::VulkanTest;

namespace
{

/// Renders `objects` (cubes) and returns the per-view draw counts (camera, then cascades).
std::array<std::uint32_t, 1 + kMaxShadowCascades> DrawCounts(rhi::IDevice& device, const SceneView& view,
                                                             const std::vector<SceneObject>& objects,
                                                             const ForwardSettings& settings)
{
    std::array<std::uint32_t, 1 + kMaxShadowCascades> counts{};
    auto readback = device.CreateBuffer(
        {.size = sizeof(counts), .usage = rhi::BufferUsage::CopyDst, .memory = rhi::MemoryUsage::Readback});
    EXPECT_TRUE(readback.has_value());
    (void)interi::testing::RenderScene(
        device, 64, view, {MakeCube(0.5f)}, objects, settings,
        [&](FrameGraph& graph, const ForwardOutputs& outputs)
        {
            const FGBuffer out = graph.ImportBuffer("count readback", *readback, rhi::ResourceState::Undefined,
                                                    rhi::ResourceState::HostRead);
            graph.AddPass(
                "ReadCounts",
                [&](PassBuilder& builder)
                {
                    builder.Read(outputs.drawCounts, rhi::ResourceState::CopySrc);
                    builder.Write(out, rhi::ResourceState::CopyDst);
                },
                [counts = outputs.drawCounts, out](PassContext& context)
                {
                    context.Commands().CopyBuffer(context.Buffer(counts), 0, context.Buffer(out), 0,
                                                  sizeof(std::array<std::uint32_t, 1 + kMaxShadowCascades>));
                });
        });
    std::memcpy(counts.data(), device.MappedMemory(*readback).data(), sizeof(counts));
    device.DestroyBuffer(*readback);
    return counts;
}

SceneView MakeView()
{
    SceneView view;
    view.cameraPosition = {0, 0, 0};
    view.view = LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0}); // looking down -Z
    view.verticalFov = 1.0f;
    return view;
}

} // namespace

TEST_F(VulkanTest, GPUCulling_OffscreenInstances_NotDrawn)
{
    // 40 cubes in front of the camera, 30 behind it and 30 far off to the side: only the first 40 are drawn.
    std::vector<SceneObject> objects;
    for (int i = 0; i < 40; ++i)
    {
        objects.push_back(
            {.mesh = 0,
             .object = {.transform = Mat4::Translation({float(i % 8) - 3.5f, float(i / 8) - 2.0f, -15.0f})}});
    }
    for (int i = 0; i < 30; ++i)
    {
        objects.push_back({.mesh = 0, .object = {.transform = Mat4::Translation({float(i % 6) - 3.0f, 0.0f, 10.0f})}});
    }
    for (int i = 0; i < 30; ++i)
    {
        objects.push_back({.mesh = 0, .object = {.transform = Mat4::Translation({200.0f + float(i), 0.0f, -15.0f})}});
    }
    const auto counts = DrawCounts(*device, MakeView(), objects, ForwardSettings{.cascadeCount = 0});
    EXPECT_EQ(counts[0], 40u);
}

TEST_F(VulkanTest, GPUCulling_SphereStraddlingTheFrustumEdge_IsDrawn)
{
    // The centre is just outside the left edge, but the bounding sphere reaches inside.
    const float edge = 15.0f * std::tan(0.5f); // half-width at distance 15 (aspect 1)
    const std::vector<SceneObject> objects{
        {.mesh = 0, .object = {.transform = Mat4::Translation({-(edge + 0.5f), 0.0f, -15.0f})}},
        {.mesh = 0, .object = {.transform = Mat4::Translation({-(edge + 3.0f), 0.0f, -15.0f})}},
    };
    const auto counts = DrawCounts(*device, MakeView(), objects, ForwardSettings{.cascadeCount = 0});
    EXPECT_EQ(counts[0], 1u);
}

TEST_F(VulkanTest, GPUCulling_ShadowViews_SkipNonCasters)
{
    // Cascades cull against their own frusta and only consider shadow casters.
    std::vector<SceneObject> objects;
    for (int i = 0; i < 10; ++i)
    {
        objects.push_back(
            {.mesh = 0,
             .object = {.transform = Mat4::Translation({float(i) - 5.0f, 0.0f, -20.0f}), .castsShadow = i < 6}});
    }
    const auto counts = DrawCounts(*device, MakeView(), objects, ForwardSettings{.cascadeCount = 1});
    EXPECT_EQ(counts[0], 10u);
    EXPECT_EQ(counts[1], 6u);
}
