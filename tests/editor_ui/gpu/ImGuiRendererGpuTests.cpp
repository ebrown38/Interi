// SPDX-License-Identifier: MIT
// The ImGui renderer on a real device: the font atlas is uploaded through ImGui's texture protocol, rectangles land
// where ImGui put them, clip rectangles become scissors, and vertex colours are linearised for sRGB targets.
#include <interi/editor_ui/ImGuiRenderer.h>
#include <interi/rhi/ResourceManager.h>

#include "ImGuiTestContext.h"
#include "OffscreenTarget.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace interi;
using interi::editor::ImGuiRenderer;
using interi::testing::OffscreenTarget;
using interi::testing::VulkanTest;

namespace
{

using Pixel = std::array<std::uint8_t, 4>;
constexpr std::uint32_t kSize = 64;

class ImGuiRendererTest : public VulkanTest
{
protected:
    /// Draws one ImGui frame (`draw` adds to the background draw list) into a kSize x kSize target of `format`.
    template <typename Draw>
    std::vector<Pixel> RenderFrame(rhi::Format format, Draw&& draw)
    {
        editor::testing::ImGuiTestContext context;
        ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(kSize), static_cast<float>(kSize));
        std::vector<Pixel> pixels;
        {
            rhi::ResourceManager resources(*device);
            auto renderer = ImGuiRenderer::Create(*device, resources, format,
                                                  ImGuiRenderer::Settings{.depthFormat = rhi::Format::D32Float});
            EXPECT_TRUE(renderer.has_value()) << renderer.error().message;
            if (!renderer)
            {
                return pixels;
            }
            ImGui::NewFrame();
            draw(*ImGui::GetBackgroundDrawList());
            ImGui::Render();
            auto prepared = (*renderer)->Prepare(*ImGui::GetDrawData());
            EXPECT_TRUE(prepared.has_value()) << prepared.error().message;
            OffscreenTarget target(*device, kSize, kSize, format);
            pixels = target.Render({0.0f, 0.0f, 0.0f, 1.0f},
                                   [&](rhi::ICommandList& commands) { (*renderer)->Record(commands, kSize, kSize); });
            device->WaitForValue(device->LastSubmittedValue());
            renderer->reset();
        }
        return pixels;
    }

    [[nodiscard]] static const Pixel& At(const std::vector<Pixel>& pixels, std::uint32_t x, std::uint32_t y)
    {
        return pixels[std::size_t{y} * kSize + x];
    }
};

} // namespace

TEST_F(ImGuiRendererTest, FilledRect_DrawnWithAtlasWhitePixel)
{
    const auto pixels = RenderFrame(rhi::Format::R8G8B8A8Unorm, [](ImDrawList& list)
                                    { list.AddRectFilled(ImVec2(8, 8), ImVec2(40, 24), IM_COL32(255, 0, 0, 255)); });
    ASSERT_EQ(pixels.size(), std::size_t{kSize} * kSize);
    EXPECT_EQ(At(pixels, 20, 16), (Pixel{255, 0, 0, 255}));
    EXPECT_EQ(At(pixels, 4, 4), (Pixel{0, 0, 0, 255}));
    EXPECT_EQ(At(pixels, 20, 30), (Pixel{0, 0, 0, 255})); // ImGui's y grows downwards
}

TEST_F(ImGuiRendererTest, ClipRect_BecomesScissor)
{
    const auto pixels = RenderFrame(rhi::Format::R8G8B8A8Unorm,
                                    [](ImDrawList& list)
                                    {
                                        list.PushClipRect(ImVec2(0, 0), ImVec2(32, 64));
                                        list.AddRectFilled(ImVec2(0, 0), ImVec2(64, 64), IM_COL32(0, 255, 0, 255));
                                        list.PopClipRect();
                                    });
    ASSERT_FALSE(pixels.empty());
    EXPECT_EQ(At(pixels, 16, 32), (Pixel{0, 255, 0, 255}));
    EXPECT_EQ(At(pixels, 48, 32), (Pixel{0, 0, 0, 255}));
}

TEST_F(ImGuiRendererTest, Text_SamplesFontAtlas)
{
    const auto pixels = RenderFrame(rhi::Format::R8G8B8A8Unorm, [](ImDrawList& list)
                                    { list.AddText(ImVec2(2, 2), IM_COL32(255, 255, 255, 255), "WWWW"); });
    ASSERT_FALSE(pixels.empty());
    // Glyph strokes from the atlas: lit pixels inside the text's bounding box, but far from all of them (the atlas'
    // white pixel would fill each glyph quad solid).
    std::uint32_t x0 = kSize, y0 = kSize, x1 = 0, y1 = 0;
    int lit = 0;
    for (std::uint32_t y = 0; y < kSize; ++y)
    {
        for (std::uint32_t x = 0; x < kSize; ++x)
        {
            if (At(pixels, x, y)[0] > 128)
            {
                ++lit;
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
        }
    }
    ASSERT_GT(lit, 20);
    const auto box = static_cast<int>((x1 - x0 + 1) * (y1 - y0 + 1));
    EXPECT_LT(lit * 10, box * 7);
    EXPECT_GE(x0, 2u);
    EXPECT_GE(y0, 2u);
}

TEST_F(ImGuiRendererTest, SrgbTarget_ColoursLinearised)
{
    const auto pixels =
        RenderFrame(rhi::Format::R8G8B8A8Srgb, [](ImDrawList& list)
                    { list.AddRectFilled(ImVec2(0, 0), ImVec2(64, 64), IM_COL32(128, 128, 128, 255)); });
    ASSERT_FALSE(pixels.empty());
    // Linearised with gamma 2.2, re-encoded with the sRGB curve: within a few steps of the original.
    EXPECT_NEAR(At(pixels, 32, 32)[0], 128, 4);
}
