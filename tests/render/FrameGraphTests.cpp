// SPDX-License-Identifier: MIT
#include <interi/render/FrameGraph.h>
#include <interi/rhi/ResourceManager.h>

#include "../rhi/MockDevice.h"
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::MockCommandList;
using interi::testing::MockDevice;
using rhi::ResourceState;
using rhi::TextureBarrier;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace
{

constexpr std::uint64_t kTextureSize = 1024;

rhi::TextureDesc ColorDesc()
{
    return {.width = 16, .height = 16, .format = rhi::Format::R8G8B8A8Unorm, .usage = rhi::TextureUsage::ColorTarget};
}

/// A device that hands out sequential handles and fixed memory requirements, recording placements.
struct FrameGraphFixture : ::testing::Test
{
    FrameGraphFixture()
    {
        ON_CALL(device, GetTextureMemoryRequirements(_))
            .WillByDefault(
                Return(rhi::MemoryRequirements{.size = kTextureSize, .alignment = 256, .memoryTypeBits = 1}));
        ON_CALL(device, GetBufferMemoryRequirements(_))
            .WillByDefault(Return(rhi::MemoryRequirements{.size = 512, .alignment = 256, .memoryTypeBits = 1}));
        ON_CALL(device, CreateHeap(_))
            .WillByDefault(
                [this](const rhi::HeapDesc& desc)
                {
                    heapSizes.push_back(desc.size);
                    return Result<rhi::HeapHandle>(
                        rhi::HeapHandle::FromParts(static_cast<std::uint32_t>(heapSizes.size()), 1));
                });
        ON_CALL(device, CreatePlacedTexture(_, _, _))
            .WillByDefault(
                [this](const rhi::TextureDesc&, rhi::HeapHandle, std::uint64_t offset)
                {
                    textureOffsets.push_back(offset);
                    return Result<rhi::TextureHandle>(
                        rhi::TextureHandle::FromParts(static_cast<std::uint32_t>(textureOffsets.size()), 1));
                });
        ON_CALL(device, CreatePlacedBuffer(_, _, _))
            .WillByDefault(
                [this](const rhi::BufferDesc&, rhi::HeapHandle, std::uint64_t offset)
                {
                    bufferOffsets.push_back(offset);
                    return Result<rhi::BufferHandle>(
                        rhi::BufferHandle::FromParts(static_cast<std::uint32_t>(bufferOffsets.size()), 1));
                });
    }

    std::vector<std::uint64_t> heapSizes;
    std::vector<std::uint64_t> textureOffsets;
    std::vector<std::uint64_t> bufferOffsets;
    NiceMock<MockDevice> device;
    NiceMock<MockCommandList> commands;
    rhi::ResourceManager resources{device}; // after device: destroyed first
};

} // namespace

TEST_F(FrameGraphFixture, FrameGraph_UnusedPass_Culled)
{
    FrameGraph graph(device, resources);
    const auto backBuffer = graph.ImportTexture("backbuffer", rhi::TextureHandle::FromParts(99, 1),
                                                ResourceState::Undefined, ResourceState::Present);
    std::vector<std::string> ran;

    auto unused = graph.CreateTexture("unused", ColorDesc());
    graph.AddPass(
        "WritesUnused", [&](PassBuilder& builder) { unused = builder.Write(unused, ResourceState::ColorTarget); },
        [&](PassContext&) { ran.emplace_back("WritesUnused"); });
    graph.AddPass(
        "WritesBackBuffer", [&](PassBuilder& builder) { builder.Write(backBuffer, ResourceState::ColorTarget); },
        [&](PassContext&) { ran.emplace_back("WritesBackBuffer"); });

    ASSERT_TRUE(graph.Compile().has_value());
    EXPECT_TRUE(graph.IsCulled("WritesUnused"));
    EXPECT_FALSE(graph.IsCulled("WritesBackBuffer"));
    ASSERT_TRUE(graph.Execute(commands).has_value());
    EXPECT_EQ(ran, (std::vector<std::string>{"WritesBackBuffer"}));
}

TEST_F(FrameGraphFixture, FrameGraph_CullingPropagatesThroughProducers)
{
    FrameGraph graph(device, resources);
    const auto backBuffer = graph.ImportTexture("backbuffer", rhi::TextureHandle::FromParts(99, 1),
                                                ResourceState::Undefined, ResourceState::Present);
    auto gbuffer = graph.CreateTexture("gbuffer", ColorDesc());
    auto debug = graph.CreateTexture("debug", ColorDesc());
    graph.AddPass(
        "GBuffer", [&](PassBuilder& builder) { gbuffer = builder.Write(gbuffer, ResourceState::ColorTarget); },
        [](PassContext&) {});
    graph.AddPass(
        "DebugView",
        [&](PassBuilder& builder)
        {
            builder.Read(gbuffer, ResourceState::ShaderRead);
            debug = builder.Write(debug, ResourceState::ColorTarget);
        },
        [](PassContext&) {});
    graph.AddPass(
        "Lighting",
        [&](PassBuilder& builder)
        {
            builder.Read(gbuffer, ResourceState::ShaderRead);
            builder.Write(backBuffer, ResourceState::ColorTarget);
        },
        [](PassContext&) {});

    ASSERT_TRUE(graph.Compile().has_value());
    EXPECT_FALSE(graph.IsCulled("GBuffer")); // needed by Lighting
    EXPECT_TRUE(graph.IsCulled("DebugView"));
    EXPECT_FALSE(graph.IsCulled("Lighting"));
}

TEST_F(FrameGraphFixture, FrameGraph_SideEffectPass_NeverCulled)
{
    FrameGraph graph(device, resources);
    bool ran = false;
    graph.AddPass("Readback", [](PassBuilder& builder) { builder.SideEffect(); }, [&](PassContext&) { ran = true; });
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute(commands).has_value());
    EXPECT_TRUE(ran);
}

TEST_F(FrameGraphFixture, FrameGraph_ReadAfterWrite_InsertsBarrier)
{
    FrameGraph graph(device, resources);
    const auto backBufferHandle = rhi::TextureHandle::FromParts(99, 1);
    const auto backBuffer =
        graph.ImportTexture("backbuffer", backBufferHandle, ResourceState::Undefined, ResourceState::Present);
    auto shadow = graph.CreateTexture("shadow", ColorDesc());
    graph.AddPass(
        "Shadow", [&](PassBuilder& builder) { shadow = builder.Write(shadow, ResourceState::ColorTarget); },
        [](PassContext&) {});
    graph.AddPass(
        "Lit",
        [&](PassBuilder& builder)
        {
            builder.Read(shadow, ResourceState::ShaderRead);
            builder.Write(backBuffer, ResourceState::ColorTarget);
        },
        [](PassContext&) {});
    ASSERT_TRUE(graph.Compile().has_value());

    std::vector<std::vector<TextureBarrier>> recorded;
    ON_CALL(commands, Barriers(_, _))
        .WillByDefault([&](auto, std::span<const TextureBarrier> textures)
                       { recorded.emplace_back(textures.begin(), textures.end()); });
    ASSERT_TRUE(graph.Execute(commands).has_value());

    const rhi::TextureHandle shadowHandle = rhi::TextureHandle::FromParts(1, 1); // first placed texture
    // Before Shadow: shadow enters ColorTarget. Before Lit: shadow ColorTarget -> ShaderRead (the RAW barrier) and
    // the back buffer enters ColorTarget. After the graph: the back buffer leaves for Present.
    ASSERT_EQ(recorded.size(), 3u);
    EXPECT_EQ(recorded[0],
              (std::vector<TextureBarrier>{{shadowHandle, ResourceState::Undefined, ResourceState::ColorTarget}}));
    EXPECT_EQ(recorded[1],
              (std::vector<TextureBarrier>{{shadowHandle, ResourceState::ColorTarget, ResourceState::ShaderRead},
                                           {backBufferHandle, ResourceState::Undefined, ResourceState::ColorTarget}}));
    EXPECT_EQ(recorded[2],
              (std::vector<TextureBarrier>{{backBufferHandle, ResourceState::ColorTarget, ResourceState::Present}}));
}

TEST_F(FrameGraphFixture, FrameGraph_ReadAfterRead_NoBarrier)
{
    FrameGraph graph(device, resources);
    const auto input = graph.ImportTexture("input", rhi::TextureHandle::FromParts(5, 1), ResourceState::ShaderRead,
                                           ResourceState::ShaderRead);
    for (const char* name : {"A", "B"})
    {
        graph.AddPass(
            name,
            [&](PassBuilder& builder)
            {
                builder.Read(input, ResourceState::ShaderRead);
                builder.SideEffect();
            },
            [](PassContext&) {});
    }
    ASSERT_TRUE(graph.Compile().has_value());
    EXPECT_CALL(commands, Barriers(_, _)).Times(0);
    ASSERT_TRUE(graph.Execute(commands).has_value());
}

TEST_F(FrameGraphFixture, FrameGraph_Aliasing_NonOverlappingLifetimesShareMemory)
{
    // A lives in passes 0-1, B in 1-2, C in 2-3: A and C never coexist, so C can reuse A's memory.
    FrameGraph graph(device, resources);
    const auto output = graph.ImportTexture("out", rhi::TextureHandle::FromParts(99, 1), ResourceState::Undefined,
                                            ResourceState::ShaderRead);
    auto a = graph.CreateTexture("A", ColorDesc());
    auto b = graph.CreateTexture("B", ColorDesc());
    auto c = graph.CreateTexture("C", ColorDesc());
    graph.AddPass(
        "P0", [&](PassBuilder& builder) { a = builder.Write(a, ResourceState::ColorTarget); }, [](PassContext&) {});
    graph.AddPass(
        "P1",
        [&](PassBuilder& builder)
        {
            builder.Read(a, ResourceState::ShaderRead);
            b = builder.Write(b, ResourceState::ColorTarget);
        },
        [](PassContext&) {});
    graph.AddPass(
        "P2",
        [&](PassBuilder& builder)
        {
            builder.Read(b, ResourceState::ShaderRead);
            c = builder.Write(c, ResourceState::ColorTarget);
        },
        [](PassContext&) {});
    graph.AddPass(
        "P3",
        [&](PassBuilder& builder)
        {
            builder.Read(c, ResourceState::ShaderRead);
            builder.Write(output, ResourceState::ColorTarget);
        },
        [](PassContext&) {});
    ASSERT_TRUE(graph.Compile().has_value());

    EXPECT_EQ(graph.TransientHeapSize(), 2 * kTextureSize); // not 3
    EXPECT_EQ(graph.TransientOffset(a), graph.TransientOffset(c));
    EXPECT_NE(graph.TransientOffset(a), graph.TransientOffset(b));

    // Memory changing hands needs an aliasing barrier; it happens once, before P2 writes C.
    EXPECT_CALL(commands, AliasingBarrier()).Times(1);
    ASSERT_TRUE(graph.Execute(commands).has_value());
    ASSERT_EQ(heapSizes.size(), 1u);
    EXPECT_EQ(heapSizes[0], 2 * kTextureSize);
}

TEST_F(FrameGraphFixture, FrameGraph_SameLayoutNextFrame_ReusesTransientResources)
{
    FrameGraph graph(device, resources);
    for (int frame = 0; frame < 3; ++frame)
    {
        graph.Reset();
        const auto output = graph.ImportTexture("out", rhi::TextureHandle::FromParts(99, 1), ResourceState::Undefined,
                                                ResourceState::ShaderRead);
        auto t = graph.CreateTexture("T", ColorDesc());
        graph.AddPass(
            "Write", [&](PassBuilder& builder) { t = builder.Write(t, ResourceState::ColorTarget); },
            [](PassContext&) {});
        graph.AddPass(
            "Read",
            [&](PassBuilder& builder)
            {
                builder.Read(t, ResourceState::ShaderRead);
                builder.Write(output, ResourceState::ColorTarget);
            },
            [](PassContext&) {});
        ASSERT_TRUE(graph.Compile().has_value());
        // Reused memory may still be in use by the previous frame on the GPU: every frame after the first starts
        // with an aliasing barrier.
        EXPECT_CALL(commands, AliasingBarrier()).Times(frame == 0 ? 0 : 1);
        ASSERT_TRUE(graph.Execute(commands).has_value());
        ::testing::Mock::VerifyAndClearExpectations(&commands);
    }
    EXPECT_EQ(heapSizes.size(), 1u);
    EXPECT_EQ(textureOffsets.size(), 1u);
}

TEST_F(FrameGraphFixture, FrameGraph_PassContext_ResolvesHandles)
{
    FrameGraph graph(device, resources);
    const auto importedHandle = rhi::TextureHandle::FromParts(42, 3);
    const auto imported =
        graph.ImportTexture("in", importedHandle, ResourceState::ShaderRead, ResourceState::ShaderRead);
    auto buffer = graph.CreateBuffer("scratch", rhi::BufferDesc{.size = 256, .usage = rhi::BufferUsage::Storage});
    rhi::TextureHandle seenTexture;
    rhi::BufferHandle seenBuffer;
    graph.AddPass(
        "Compute",
        [&](PassBuilder& builder)
        {
            builder.Read(imported, ResourceState::ShaderRead);
            buffer = builder.Write(buffer, ResourceState::ShaderWrite);
            builder.SideEffect();
        },
        [&](PassContext& context)
        {
            seenTexture = context.Texture(imported);
            seenBuffer = context.Buffer(buffer);
            EXPECT_EQ(&context.Commands(), &commands);
        });
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute(commands).has_value());
    EXPECT_EQ(seenTexture, importedHandle);
    EXPECT_EQ(seenBuffer, rhi::BufferHandle::FromParts(1, 1));
}

TEST_F(FrameGraphFixture, FrameGraph_ReadOfNeverWrittenTransient_IsError)
{
    FrameGraph graph(device, resources);
    const auto t = graph.CreateTexture("T", ColorDesc());
    graph.AddPass(
        "Reads",
        [&](PassBuilder& builder)
        {
            builder.Read(t, ResourceState::ShaderRead);
            builder.SideEffect();
        },
        [](PassContext&) {});
    EXPECT_FALSE(graph.Compile().has_value());
}
