// SPDX-License-Identifier: MIT
#include <interi/asset_import/TextureImport.h>

#include "ImportFixtures.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::asset_import;
using namespace interi::testing_fixtures;

namespace
{

std::vector<std::uint8_t> Pixels4x2()
{
    return {255, 0,  0,  255, 0,  255, 0,  255, 0,  0,  255, 255, 255, 255, 255, 128,
            10,  20, 30, 255, 40, 50,  60, 255, 70, 80, 90,  255, 100, 110, 120, 0};
}

} // namespace

TEST(DecodeImage, Png_RoundTripsExactly)
{
    auto image = DecodeImage(MakePng(4, 2, Pixels4x2()));
    ASSERT_TRUE(image.has_value()) << image.error().message;
    EXPECT_FALSE(image->isHdr);
    EXPECT_TRUE(image->hasAlpha);
    EXPECT_EQ(image->ldr.width, 4u);
    EXPECT_EQ(image->ldr.height, 2u);
    EXPECT_EQ(image->ldr.texels, Pixels4x2());
}

TEST(DecodeImage, Tga_FlipsToTopRowFirst)
{
    // Bottom row first: row 0 (bottom) is red, row 1 (top) is blue.
    auto image = DecodeImage(MakeTga(1, 2, {0, 0, 255, 255, 0, 0}));
    ASSERT_TRUE(image.has_value()) << image.error().message;
    ASSERT_EQ(image->ldr.texels.size(), 8u);
    EXPECT_EQ(image->ldr.texels[0], 0); // top texel: blue
    EXPECT_EQ(image->ldr.texels[2], 255);
    EXPECT_EQ(image->ldr.texels[4], 255); // bottom texel: red
    EXPECT_EQ(image->ldr.texels[6], 0);
    EXPECT_EQ(image->ldr.texels[3], 255);
}

TEST(DecodeImage, Hdr_DecodesToFloats)
{
    auto image = DecodeImage(MakeHdr(4, 2));
    ASSERT_TRUE(image.has_value()) << image.error().message;
    ASSERT_TRUE(image->isHdr);
    EXPECT_EQ(image->hdr.width, 4u);
    ASSERT_EQ(image->hdr.texels.size(), 32u);
    EXPECT_NEAR(image->hdr.texels[0], 1.0f, 1e-6f);
    EXPECT_NEAR(image->hdr.texels[1], 0.5f, 1e-6f);
    EXPECT_NEAR(image->hdr.texels[2], 0.25f, 1e-6f);
}

TEST(DecodeImage, NotAnImage_IsCorrupt)
{
    EXPECT_EQ(DecodeImage(Bytes("hello world, this is not an image")).error().code, ErrorCode::Corrupt);
    EXPECT_EQ(DecodeImage({}).error().code, ErrorCode::Corrupt);
}

TEST(DecodeImage, TruncatedPng_IsCorrupt)
{
    std::vector<std::byte> png = MakePng(4, 2, Pixels4x2());
    png.resize(png.size() / 2);
    EXPECT_FALSE(DecodeImage(png).has_value());
}

TEST(DecodeImage, HugeDimensions_AreRefusedBeforeDecoding)
{
    std::vector<std::byte> png = MakePng(4, 2, Pixels4x2());
    // Rewrite the IHDR width and height (offsets 16 and 20) to 100000 and fix nothing else: the dimension check fires
    // from the header.
    for (const std::size_t offset : {std::size_t{16}, std::size_t{20}})
    {
        png[offset] = std::byte{0};
        png[offset + 1] = std::byte{0x01};
        png[offset + 2] = std::byte{0x86};
        png[offset + 3] = std::byte{0xA0};
    }
    auto image = DecodeImage(png);
    ASSERT_FALSE(image.has_value());
    EXPECT_EQ(image.error().code, ErrorCode::Corrupt);
}

TEST(ImportTexture, BuildsMipsInTheRequestedColourSpace)
{
    auto texture = ImportTexture(MakePng(4, 2, Pixels4x2()), {});
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ(texture->colorSpace, assets::ColorSpace::Srgb);
    EXPECT_EQ(texture->mips.size(), 3u);
    EXPECT_TRUE(assets::ValidateTexture(*texture).has_value());
    auto linear =
        ImportTexture(MakePng(4, 2, Pixels4x2()), {.colorSpace = assets::ColorSpace::Linear, .generateMips = false});
    ASSERT_TRUE(linear.has_value());
    EXPECT_EQ(linear->colorSpace, assets::ColorSpace::Linear);
    EXPECT_EQ(linear->mips.size(), 1u);
}

TEST(ImportTexture, FlipY_MirrorsTheRows)
{
    auto plain = ImportTexture(MakePng(4, 2, Pixels4x2()), {.generateMips = false});
    auto flipped = ImportTexture(MakePng(4, 2, Pixels4x2()), {.generateMips = false, .flipY = true});
    ASSERT_TRUE(plain.has_value());
    ASSERT_TRUE(flipped.has_value());
    auto a = assets::DecodeMip(*plain, 0);
    auto b = assets::DecodeMip(*flipped, 0);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    for (std::size_t i = 0; i < 16; ++i)
    {
        EXPECT_EQ(a->texels[i], b->texels[16 + i]);
        EXPECT_EQ(a->texels[16 + i], b->texels[i]);
    }
}

TEST(ImportTexture, Compression_ProducesBlockData)
{
    auto texture = ImportTexture(MakePng(8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 200)),
                                 {.generateMips = true, .compression = assets::TextureCompression::Bc1});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->format, assets::TextureFormat::Bc1);
    EXPECT_EQ(texture->mips[0].size(), 4u * 8u);
}

TEST(ImportTexture, Hdr_IsFloatAndLinear)
{
    auto texture = ImportTexture(MakeHdr(4, 2), {.colorSpace = assets::ColorSpace::Srgb});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->format, assets::TextureFormat::Rgba32F);
    EXPECT_EQ(texture->colorSpace, assets::ColorSpace::Linear);
}

TEST(ImportTexture, IsImagePath_RecognisesTheFormats)
{
    EXPECT_TRUE(IsImagePath("a/b.PNG"));
    EXPECT_TRUE(IsImagePath("a.jpeg"));
    EXPECT_TRUE(IsImagePath("a.hdr"));
    EXPECT_FALSE(IsImagePath("a.psd"));
}
