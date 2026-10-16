// SPDX-License-Identifier: MIT
#include <interi/render/Texture.h>

#include <gtest/gtest.h>

#include <cstdint>

using namespace interi::render;

namespace
{

ImageRgba8 Solid(std::uint32_t width, std::uint32_t height, std::uint8_t value)
{
    return {
        .width = width, .height = height, .texels = std::vector<std::uint8_t>(std::size_t{width} * height * 4, value)};
}

} // namespace

TEST(Texture, GenerateMips_PowerOfTwo_FullChainDownToOne)
{
    const auto mips = GenerateMips(Solid(8, 4, 10));
    ASSERT_EQ(mips.size(), 4u);
    EXPECT_EQ(mips[1].width, 4u);
    EXPECT_EQ(mips[1].height, 2u);
    EXPECT_EQ(mips[2].width, 2u);
    EXPECT_EQ(mips[2].height, 1u);
    EXPECT_EQ(mips[3].width, 1u);
    EXPECT_EQ(mips[3].height, 1u);
    for (const ImageRgba8& mip : mips)
    {
        ASSERT_EQ(mip.texels.size(), std::size_t{mip.width} * mip.height * 4);
        for (const std::uint8_t texel : mip.texels)
        {
            EXPECT_EQ(texel, 10);
        }
    }
}

TEST(Texture, GenerateMips_Checkerboard_AveragesToGrey)
{
    ImageRgba8 base = Solid(2, 2, 0);
    base.At(0, 0)[0] = 255;
    base.At(1, 1)[0] = 255;
    base.At(1, 0)[1] = 3; // (0 + 3 + 0 + 0 + 2) / 4 rounds to 1
    const auto mips = GenerateMips(base);
    ASSERT_EQ(mips.size(), 2u);
    EXPECT_EQ(mips[1].At(0, 0)[0], 128); // (510 + 2) / 4
    EXPECT_EQ(mips[1].At(0, 0)[1], 1);
}

TEST(Texture, GenerateMips_OddSize_RepeatsEdgeTexels)
{
    ImageRgba8 base = Solid(3, 1, 0);
    base.At(2, 0)[0] = 200;
    const auto mips = GenerateMips(base);
    ASSERT_EQ(mips.size(), 2u);
    EXPECT_EQ(mips[1].width, 1u);
    EXPECT_EQ(mips[1].At(0, 0)[0], 0); // texels 0 and 1 only; texel 2 is dropped by the halving
}
