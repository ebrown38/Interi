// SPDX-License-Identifier: MIT
// Golden image tests of the forward renderer, rendered on lavapipe.
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/render/Camera.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Mesh.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include "GoldenImage.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <numbers>

using namespace interi;
using namespace interi::render;
using interi::testing::Image;
using interi::testing::VulkanTest;

namespace
{

constexpr std::uint32_t kSize = 128;
constexpr std::uint32_t kRowPitch = kSize * 4; // already a multiple of kTextureRowPitchAlignment

/// Renders `objects` (uploading `meshes` first) to a kSize x kSize sRGB image through the frame graph.
Image RenderScene(rhi::IDevice& device, const SceneView& view, const std::vector<MeshData>& meshes,
                  const std::vector<std::pair<std::size_t, RenderObject>>& objects)
{
    auto color = device.CreateTexture({.width = kSize,
                                       .height = kSize,
                                       .format = rhi::Format::R8G8B8A8Srgb,
                                       .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::CopySrc});
    auto readback = device.CreateBuffer(
        {.size = kRowPitch * kSize, .usage = rhi::BufferUsage::CopyDst, .memory = rhi::MemoryUsage::Readback});
    auto pool = device.CreateCommandPool();
    EXPECT_TRUE(color && readback && pool);

    Image image{kSize, kSize, {}};
    {
        rhi::ResourceManager resources(device);
        rhi::UploadQueue uploads(device, 1024 * 1024);
        auto renderer = ForwardRenderer::Create(device, resources, rhi::Format::R8G8B8A8Srgb, 1);
        EXPECT_TRUE(renderer.has_value()) << renderer.error().message;
        rhi::ICommandList& commands = device.BeginCommandList(*pool);

        std::vector<GpuMesh> gpuMeshes;
        for (const MeshData& mesh : meshes)
        {
            auto uploaded = UploadMesh(device, uploads, commands, mesh);
            EXPECT_TRUE(uploaded.has_value());
            gpuMeshes.push_back(*uploaded);
        }
        std::vector<RenderObject> drawList;
        for (const auto& [meshIndex, object] : objects)
        {
            drawList.push_back(object);
            drawList.back().mesh = &gpuMeshes[meshIndex];
        }

        FrameGraph graph(device, resources);
        FGTexture target =
            graph.ImportTexture("target", *color, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc);
        (*renderer)->AddPasses(graph, target, kSize, kSize, view, drawList, {0.10f, 0.12f, 0.15f});
        EXPECT_TRUE(graph.Compile().has_value());
        EXPECT_TRUE(graph.Execute(commands).has_value());

        const std::array toCopy{
            rhi::BufferBarrier{*readback, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
        commands.Barriers(toCopy, {});
        commands.CopyTextureToBuffer(*color, *readback, 0, kRowPitch);
        const std::array toHost{
            rhi::BufferBarrier{*readback, rhi::ResourceState::CopyDst, rhi::ResourceState::HostRead}};
        commands.Barriers(toHost, {});
        rhi::ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device.Submit(lists);
        uploads.Submitted(value);
        device.WaitForValue(value);

        image.pixels.resize(std::size_t{kSize} * kSize);
        std::memcpy(image.pixels.data(), device.MappedMemory(*readback).data(), image.pixels.size() * 4);
        for (const GpuMesh& mesh : gpuMeshes)
        {
            DestroyMesh(resources, mesh);
        }
    }
    device.DestroyCommandPool(*pool);
    device.DestroyBuffer(*readback);
    device.DestroyTexture(*color);
    return image;
}

SceneView MakeView(Vec3 eye)
{
    SceneView view;
    view.cameraPosition = eye;
    view.view = LookAt(eye, {0, 0, 0}, {0, 1, 0});
    view.projection = PerspectiveReverseZ(std::numbers::pi_v<float> / 3.0f, 1.0f, 0.1f);
    view.sun = {.direction = {-0.4f, -1.0f, -0.6f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};
    return view;
}

} // namespace

TEST_F(VulkanTest, Render_LitCube_MatchesGolden)
{
    const RenderObject cube{.mesh = nullptr,
                            .transform =
                                Mat4::Rotation(Quat::FromAxisAngle({0, 1, 0}, -std::numbers::pi_v<float> / 9.0f)),
                            .material = {.baseColor = {0.8f, 0.3f, 0.2f}, .metallic = 0.0f, .roughness = 0.4f}};
    const Image image = RenderScene(*device, MakeView({2.2f, 1.8f, 3.0f}), {MakeCube(0.6f)}, {{0, cube}});
    interi::testing::ExpectMatchesGolden(image, "lit_cube");
}
