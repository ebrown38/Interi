// SPDX-License-Identifier: MIT
#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/testing/StateHasher.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <span>

using namespace interi;
using namespace interi::destruction;

namespace
{

std::uint64_t Hash(const render::ImageRgba8& image)
{
    interi::testing::StateHasher hasher;
    hasher.AddBytes(std::as_bytes(std::span(image.texels)));
    return hasher.Finish();
}

/// Mean absolute difference (all channels) between columns a and b.
double ColumnDifference(const render::ImageRgba8& image, std::uint32_t a, std::uint32_t b)
{
    double total = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        for (std::uint32_t c = 0; c < 3; ++c)
        {
            total += std::abs(int{image.At(a, y)[c]} - int{image.At(b, y)[c]});
        }
    }
    return total / (image.height * 3.0);
}

} // namespace

TEST(InteriorTexture, SameSeed_SameTexels)
{
    for (std::uint32_t p = 0; p < kInteriorPatternCount; ++p)
    {
        const auto pattern = static_cast<InteriorPattern>(p);
        EXPECT_EQ(Hash(MakeInteriorTexture(pattern, 128, 3)), Hash(MakeInteriorTexture(pattern, 128, 3))) << p;
        EXPECT_NE(Hash(MakeInteriorTexture(pattern, 128, 3)), Hash(MakeInteriorTexture(pattern, 128, 4))) << p;
    }
}

TEST(InteriorTexture, Concrete_PinnedAcrossPlatforms)
{
    // Integer-only generation: this hash must match on every compiler. Changing the pattern changes it.
    EXPECT_EQ(Hash(MakeInteriorTexture(InteriorPattern::Concrete, 128, 1)), 1348286182151429292ull);
}

TEST(InteriorTexture, EveryPattern_TilesSeamlessly)
{
    for (std::uint32_t p = 0; p < kInteriorPatternCount; ++p)
    {
        const render::ImageRgba8 image = MakeInteriorTexture(static_cast<InteriorPattern>(p), 128, 1);
        double neighbours = 0.0;
        for (std::uint32_t x = 0; x + 1 < 128; ++x)
        {
            neighbours += ColumnDifference(image, x, x + 1);
        }
        neighbours /= 127.0;
        // Wrapping from the last column to the first is no rougher than any other step.
        EXPECT_LE(ColumnDifference(image, 127, 0), neighbours * 3.0 + 2.0) << "pattern " << p;
    }
}

TEST(InteriorTexture, Concrete_HasAggregateAndCement)
{
    const render::ImageRgba8 image = MakeInteriorTexture(InteriorPattern::Concrete, 128, 1);
    std::uint32_t dark = 0;
    std::uint32_t light = 0;
    for (std::size_t i = 0; i < image.texels.size(); i += 4)
    {
        const int luma = (image.texels[i] + image.texels[i + 1] + image.texels[i + 2]) / 3;
        dark += luma < 130 ? 1u : 0u;
        light += luma >= 140 ? 1u : 0u;
        EXPECT_EQ(image.texels[i + 3], 255);
    }
    const std::uint32_t total = 128 * 128;
    EXPECT_GT(dark, total / 20); // stones
    EXPECT_GT(light, total / 2); // cement
}
