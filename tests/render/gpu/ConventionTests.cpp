// SPDX-License-Identifier: MIT
// Pins the rasterization conventions every renderer relies on, one pixel at a time, before any golden image:
// clip space is +Y up, counter-clockwise triangles are front faces, and depth is reverse-Z.
#include <interi/rhi/UploadQueue.h>
#include <interi/shaders/ClipSpace.vs.h>
#include <interi/shaders/Solid.ps.h>

#include "OffscreenTarget.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace interi;
using namespace interi::rhi;
using interi::testing::OffscreenTarget;
using interi::testing::VulkanTest;

namespace
{

struct Push
{
    std::uint32_t vertices;
    std::uint32_t pad[3];
    std::array<float, 4> color;
};

using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel kRed{255, 0, 0, 255};
constexpr Pixel kGreen{0, 255, 0, 255};
constexpr Pixel kBlack{0, 0, 0, 255};
constexpr std::uint32_t kSize = 64;

class ConventionTest : public VulkanTest
{
protected:
    void SetUp() override
    {
        VulkanTest::SetUp();
        if (device == nullptr)
        {
            return;
        }
        const std::array formats{Format::R8G8B8A8Unorm};
        auto pipeline = device->CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kClipSpaceVs)),
                                                        .pixelCode = std::as_bytes(std::span(kSolidPs)),
                                                        .colorFormats = formats,
                                                        .depthFormat = Format::D32Float});
        ASSERT_TRUE(pipeline.has_value()) << pipeline.error().message;
        m_pipeline = *pipeline;
        auto vertices =
            device->CreateBuffer({.size = 64 * 16, .usage = BufferUsage::Storage, .memory = MemoryUsage::Upload});
        ASSERT_TRUE(vertices.has_value());
        m_vertices = *vertices;
        m_target = std::make_unique<OffscreenTarget>(*device, kSize, kSize);
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            m_target.reset();
            device->DestroyBuffer(m_vertices);
            device->DestroyPipeline(m_pipeline);
        }
        VulkanTest::TearDown();
    }

    /// Draws triangles (clip-space float4 vertices, three per triangle), each in its own colour, in order.
    std::vector<Pixel> DrawTriangles(const std::vector<std::array<float, 4>>& vertices,
                                     const std::vector<std::array<float, 4>>& colors)
    {
        std::memcpy(device->MappedMemory(m_vertices).data(), vertices.data(), vertices.size() * 16);
        return m_target->Render({0, 0, 0, 1},
                                [&](ICommandList& commands)
                                {
                                    commands.BindPipeline(m_pipeline);
                                    for (std::uint32_t t = 0; t < colors.size(); ++t)
                                    {
                                        commands.PushConstants(Push{.vertices = device->BindlessIndex(m_vertices),
                                                                    .pad = {},
                                                                    .color = colors[t]});
                                        commands.Draw(3, 1, t * 3, 0);
                                    }
                                });
    }

    static Pixel At(const std::vector<Pixel>& pixels, std::uint32_t x, std::uint32_t y)
    {
        return pixels[std::size_t{y} * kSize + x];
    }

    PipelineHandle m_pipeline;
    BufferHandle m_vertices;
    std::unique_ptr<OffscreenTarget> m_target;
};

} // namespace

TEST_F(ConventionTest, CounterClockwiseTriangle_IsFrontFacing)
{
    // Counter-clockwise in clip space (+Y up), depth 0.5: drawn with back-face culling.
    const auto pixels =
        DrawTriangles({{-0.8f, -0.8f, 0.5f, 1}, {0.8f, -0.8f, 0.5f, 1}, {0.0f, 0.8f, 0.5f, 1}}, {{1, 0, 0, 1}});
    EXPECT_EQ(At(pixels, kSize / 2, kSize / 2), kRed);
}

TEST_F(ConventionTest, ClockwiseTriangle_IsCulled)
{
    const auto pixels =
        DrawTriangles({{-0.8f, -0.8f, 0.5f, 1}, {0.0f, 0.8f, 0.5f, 1}, {0.8f, -0.8f, 0.5f, 1}}, {{1, 0, 0, 1}});
    EXPECT_EQ(At(pixels, kSize / 2, kSize / 2), kBlack);
}

TEST_F(ConventionTest, PositiveClipY_IsTopOfImage)
{
    // A triangle entirely in the upper half of clip space (y > 0) covers the top rows of the image (row 0 = top).
    const auto pixels =
        DrawTriangles({{-1.0f, 0.1f, 0.5f, 1}, {1.0f, 0.1f, 0.5f, 1}, {0.0f, 1.0f, 0.5f, 1}}, {{1, 0, 0, 1}});
    EXPECT_EQ(At(pixels, kSize / 2, 4), kRed);
    EXPECT_EQ(At(pixels, kSize / 2, kSize - 4), kBlack);
}

TEST_F(ConventionTest, ReverseZ_NearerTriangleWinsWhicheverIsDrawnFirst)
{
    // Larger depth is nearer. The near (red, z = 0.8) triangle is drawn first; the far one (green, z = 0.2) must
    // fail the depth test even though it is drawn later.
    const std::vector<std::array<float, 4>> vertices{{-0.8f, -0.8f, 0.8f, 1}, {0.8f, -0.8f, 0.8f, 1},
                                                     {0.0f, 0.8f, 0.8f, 1},   {-0.8f, -0.8f, 0.2f, 1},
                                                     {0.8f, -0.8f, 0.2f, 1},  {0.0f, 0.8f, 0.2f, 1}};
    const auto pixels = DrawTriangles(vertices, {{1, 0, 0, 1}, {0, 1, 0, 1}});
    EXPECT_EQ(At(pixels, kSize / 2, kSize / 2), kRed);
}

TEST_F(ConventionTest, ReverseZ_ClearDepthZero_IsFarthest)
{
    // Depth clears to 0 (the far plane): anything drawn in front of it passes, even at z = 0.01.
    const auto pixels =
        DrawTriangles({{-0.8f, -0.8f, 0.01f, 1}, {0.8f, -0.8f, 0.01f, 1}, {0.0f, 0.8f, 0.01f, 1}}, {{0, 1, 0, 1}});
    EXPECT_EQ(At(pixels, kSize / 2, kSize / 2), kGreen);
}
