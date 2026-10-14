// SPDX-License-Identifier: MIT
// The frame graph on a real device: transient buffers are placed (and alias) in one heap, are reused on the next
// frame, and the data survives the chain under synchronization validation. (Whether the aliasing barrier is emitted
// is checked by the mock tests; in this all-transfer chain the per-resource barriers already order the accesses.)
#include <interi/render/FrameGraph.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::VulkanTest;
using rhi::BufferUsage;
using rhi::ResourceState;

TEST_F(VulkanTest, FrameGraph_AliasedTransients_CopyChainReadsBack)
{
    constexpr std::uint64_t kSize = 64 * 1024;
    std::vector<std::byte> source(kSize);
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        source[i] = static_cast<std::byte>((i * 7u + 3u) & 0xFFu);
    }
    auto readback =
        device->CreateBuffer({.size = kSize, .usage = BufferUsage::CopyDst, .memory = rhi::MemoryUsage::Readback});
    auto pool = device->CreateCommandPool();
    ASSERT_TRUE(readback && pool);

    {
        rhi::ResourceManager resources(*device);
        rhi::UploadQueue uploads(*device, kSize);
        FrameGraph graph(*device, resources);
        for (int frame = 0; frame < 2; ++frame) // the second frame reuses the placed resources
        {
            device->ResetCommandPool(*pool);
            graph.Reset();
            const rhi::BufferDesc desc{.size = kSize, .usage = BufferUsage::CopySrc | BufferUsage::CopyDst};
            auto a = graph.CreateBuffer("A", desc);
            auto b = graph.CreateBuffer("B", desc);
            auto c = graph.CreateBuffer("C", desc);
            const auto out =
                graph.ImportBuffer("readback", *readback, ResourceState::Undefined, ResourceState::HostRead);
            graph.AddPass(
                "Upload", [&](PassBuilder& builder) { a = builder.Write(a, ResourceState::CopyDst); },
                [&](PassContext& context)
                { ASSERT_TRUE(uploads.UploadBuffer(context.Commands(), context.Buffer(a), 0, source)); });
            const auto copy = [](FGBuffer from, FGBuffer to)
            {
                return [from, to](PassContext& context)
                { context.Commands().CopyBuffer(context.Buffer(from), 0, context.Buffer(to), 0, kSize); };
            };
            graph.AddPass(
                "AtoB",
                [&](PassBuilder& builder)
                {
                    builder.Read(a, ResourceState::CopySrc);
                    b = builder.Write(b, ResourceState::CopyDst);
                },
                copy(a, b));
            // C's lifetime starts after A's ends, so C takes over A's memory.
            const FGBuffer bRead = b;
            graph.AddPass(
                "BtoC",
                [&](PassBuilder& builder)
                {
                    builder.Read(b, ResourceState::CopySrc);
                    c = builder.Write(c, ResourceState::CopyDst);
                },
                copy(bRead, c));
            graph.AddPass(
                "CtoOut",
                [&](PassBuilder& builder)
                {
                    builder.Read(c, ResourceState::CopySrc);
                    builder.Write(out, ResourceState::CopyDst);
                },
                copy(c, out));
            ASSERT_TRUE(graph.Compile().has_value());
            EXPECT_EQ(graph.TransientOffset(a), graph.TransientOffset(c));

            rhi::ICommandList& commands = device->BeginCommandList(*pool);
            ASSERT_TRUE(graph.Execute(commands).has_value());
            rhi::ICommandList* const lists[] = {&commands};
            const std::uint64_t value = device->Submit(lists);
            uploads.Submitted(value);
            device->WaitForValue(value);
            EXPECT_EQ(std::memcmp(device->MappedMemory(*readback).data(), source.data(), kSize), 0);
            resources.Collect();
        }
    }
    device->DestroyCommandPool(*pool);
    device->DestroyBuffer(*readback);
}

TEST_F(VulkanTest, FrameGraph_ReusedTransients_BackToBackFramesAreOrdered)
{
    // Two frames in flight: frame 2 reuses frame 1's transient buffer while frame 1 may still be running, so the
    // graph must order frame 2's first write after frame 1's accesses.
    constexpr std::uint64_t kSize = 4096;
    auto source =
        device->CreateBuffer({.size = kSize, .usage = BufferUsage::CopySrc, .memory = rhi::MemoryUsage::Upload});
    auto out = device->CreateBuffer({.size = kSize * 2, .usage = BufferUsage::CopyDst});
    ASSERT_TRUE(source && out);
    std::array<rhi::CommandPoolHandle, 2> pools{};
    for (auto& pool : pools)
    {
        auto created = device->CreateCommandPool();
        ASSERT_TRUE(created.has_value());
        pool = *created;
    }
    {
        rhi::ResourceManager resources(*device);
        FrameGraph graph(*device, resources);
        std::uint64_t last = 0;
        for (std::uint64_t frame = 0; frame < 2; ++frame)
        {
            graph.Reset();
            auto scratch = graph.CreateBuffer(
                "scratch", rhi::BufferDesc{.size = kSize, .usage = BufferUsage::CopySrc | BufferUsage::CopyDst});
            const auto output = graph.ImportBuffer(
                "out", *out, frame == 0 ? ResourceState::Undefined : ResourceState::CopyDst, ResourceState::CopyDst);
            graph.AddPass(
                "Fill", [&](PassBuilder& builder) { scratch = builder.Write(scratch, ResourceState::CopyDst); },
                [&](PassContext& context)
                { context.Commands().CopyBuffer(*source, 0, context.Buffer(scratch), 0, kSize); });
            const FGBuffer filled = scratch;
            graph.AddPass(
                "Out",
                [&](PassBuilder& builder)
                {
                    builder.Read(filled, ResourceState::CopySrc);
                    builder.Write(output, ResourceState::CopyDst);
                },
                [&, frame](PassContext& context)
                { context.Commands().CopyBuffer(context.Buffer(filled), 0, *out, frame * kSize, kSize); });
            ASSERT_TRUE(graph.Compile().has_value());
            rhi::ICommandList& commands = device->BeginCommandList(pools[frame]);
            ASSERT_TRUE(graph.Execute(commands).has_value());
            rhi::ICommandList* const lists[] = {&commands};
            last = device->Submit(lists); // no wait between frames
        }
        device->WaitForValue(last);
    }
    for (const auto pool : pools)
    {
        device->DestroyCommandPool(pool);
    }
    device->DestroyBuffer(*source);
    device->DestroyBuffer(*out);
}
