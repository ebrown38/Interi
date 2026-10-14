// SPDX-License-Identifier: MIT
#pragma once

#include <interi/render/ForwardRenderer.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Mesh.h>
#include <interi/render/MeshPool.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include "GoldenImage.h"
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace interi::testing
{

struct SceneObject
{
    std::size_t mesh = 0; ///< Index into the meshes passed to RenderScene (fills object.mesh).
    render::RenderObject object;
};

/// Renders a scene with the forward renderer into a size x size sRGB image and reads it back. `afterForward` can add
/// passes that use the forward outputs (e.g. to read back the light clusters).
inline Image
RenderScene(rhi::IDevice& device, std::uint32_t size, const render::SceneView& view,
            const std::vector<render::MeshData>& meshes, const std::vector<SceneObject>& objects,
            render::ForwardSettings settings = {},
            const std::function<void(render::FrameGraph&, const render::ForwardOutputs&)>& afterForward = {})
{
    const std::uint32_t rowPitch = (size * 4 + rhi::kTextureRowPitchAlignment - 1) / rhi::kTextureRowPitchAlignment *
                                   rhi::kTextureRowPitchAlignment;
    auto color = device.CreateTexture({.width = size,
                                       .height = size,
                                       .format = rhi::Format::R8G8B8A8Srgb,
                                       .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::CopySrc});
    auto readback = device.CreateBuffer({.size = std::uint64_t{rowPitch} * size,
                                         .usage = rhi::BufferUsage::CopyDst,
                                         .memory = rhi::MemoryUsage::Readback});
    auto pool = device.CreateCommandPool();
    EXPECT_TRUE(color && readback && pool);

    Image image{size, size, {}};
    {
        rhi::ResourceManager resources(device);
        rhi::UploadQueue uploads(device, 1024 * 1024);
        auto meshPool = render::MeshPool::Create(device, resources, 64 * 1024, 256 * 1024);
        EXPECT_TRUE(meshPool.has_value());
        settings.framesInFlight = 1;
        auto renderer =
            render::ForwardRenderer::Create(device, resources, **meshPool, rhi::Format::R8G8B8A8Srgb, settings);
        EXPECT_TRUE(renderer.has_value()) << renderer.error().message;
        rhi::ICommandList& commands = device.BeginCommandList(*pool);

        std::vector<render::MeshRange> ranges;
        for (const render::MeshData& mesh : meshes)
        {
            auto added = (*meshPool)->Add(uploads, commands, mesh);
            EXPECT_TRUE(added.has_value());
            ranges.push_back(*added);
        }
        std::vector<render::RenderObject> drawList;
        for (const SceneObject& entry : objects)
        {
            drawList.push_back(entry.object);
            drawList.back().mesh = ranges[entry.mesh];
        }

        render::FrameGraph graph(device, resources);
        render::FGTexture target =
            graph.ImportTexture("target", *color, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc);
        const render::ForwardOutputs outputs =
            (*renderer)->AddPasses(graph, target, size, size, view, drawList, {0.10f, 0.12f, 0.15f});
        if (afterForward)
        {
            afterForward(graph, outputs);
        }
        EXPECT_TRUE(graph.Compile().has_value());
        EXPECT_TRUE(graph.Execute(commands).has_value());

        const std::array toCopy{
            rhi::BufferBarrier{*readback, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
        commands.Barriers(toCopy, {});
        commands.CopyTextureToBuffer(*color, *readback, 0, rowPitch);
        const std::array toHost{
            rhi::BufferBarrier{*readback, rhi::ResourceState::CopyDst, rhi::ResourceState::HostRead}};
        commands.Barriers(toHost, {});
        rhi::ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device.Submit(lists);
        uploads.Submitted(value);
        device.WaitForValue(value);

        image.pixels.resize(std::size_t{size} * size);
        const std::byte* mapped = device.MappedMemory(*readback).data();
        for (std::uint32_t y = 0; y < size; ++y)
        {
            std::memcpy(&image.pixels[std::size_t{y} * size], mapped + std::size_t{y} * rowPitch, size * 4ull);
        }
    }
    device.DestroyCommandPool(*pool);
    device.DestroyBuffer(*readback);
    device.DestroyTexture(*color);
    return image;
}

} // namespace interi::testing
