// SPDX-License-Identifier: MIT
#include <interi/assets/Texture.h>
#include <interi/platform/FileSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace interi;
using namespace interi::assets;

namespace
{

Image8 Solid(std::uint32_t w, std::uint32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
    Image8 image{.width = w, .height = h, .texels = {}};
    for (std::uint32_t i = 0; i < w * h; ++i)
    {
        image.texels.insert(image.texels.end(), {r, g, b, a});
    }
    return image;
}

/// A smooth diagonal gradient: the kind of content block compression handles well.
Image8 Gradient(std::uint32_t w, std::uint32_t h)
{
    Image8 image{.width = w, .height = h, .texels = {}};
    for (std::uint32_t y = 0; y < h; ++y)
    {
        for (std::uint32_t x = 0; x < w; ++x)
        {
            const auto v = static_cast<std::uint8_t>((x * 255) / std::max(1u, w - 1));
            const auto u = static_cast<std::uint8_t>((y * 255) / std::max(1u, h - 1));
            image.texels.insert(image.texels.end(), {v, u, static_cast<std::uint8_t>((v + u) / 2), 255});
        }
    }
    return image;
}

int MaxChannelError(const Image8& a, const Image8& b, std::size_t channels)
{
    int worst = 0;
    for (std::size_t i = 0; i < a.texels.size(); ++i)
    {
        if (i % 4 < channels)
        {
            worst = std::max(worst, std::abs(int{a.texels[i]} - int{b.texels[i]}));
        }
    }
    return worst;
}

} // namespace

TEST(Texture, MipCount_CountsDownToOneTexel)
{
    EXPECT_EQ(MipCount(1, 1), 1u);
    EXPECT_EQ(MipCount(4, 4), 3u);
    EXPECT_EQ(MipCount(5, 3), 3u);
    EXPECT_EQ(MipCount(256, 16), 9u);
    EXPECT_EQ(MipCount(0, 4), 0u);
}

TEST(Texture, MipByteSize_RoundsBlocksUp)
{
    EXPECT_EQ(MipByteSize(TextureFormat::Rgba8, 3, 2), 24u);
    EXPECT_EQ(MipByteSize(TextureFormat::Rgba32F, 3, 2), 96u);
    EXPECT_EQ(MipByteSize(TextureFormat::Bc1, 1, 1), 8u);
    EXPECT_EQ(MipByteSize(TextureFormat::Bc1, 5, 4), 16u);
    EXPECT_EQ(MipByteSize(TextureFormat::Bc5, 8, 8), 64u);
}

TEST(Mips, LinearBoxFilter_IsExactIntegerAverage)
{
    Image8 base{.width = 2, .height = 2, .texels = {}};
    base.texels = {0, 10, 20, 255, 4, 10, 20, 255, 4, 10, 20, 255, 5, 10, 21, 255};
    const auto chain = GenerateMips(base, ColorSpace::Linear);
    ASSERT_EQ(chain.size(), 2u);
    ASSERT_EQ(chain[1].texels.size(), 4u);
    EXPECT_EQ(chain[1].texels[0], 3); // (0 + 4 + 4 + 5 + 2) / 4 rounds 13/4 = 3.25 down after the +2 bias
    EXPECT_EQ(chain[1].texels[1], 10);
    EXPECT_EQ(chain[1].texels[2], 20);
    EXPECT_EQ(chain[1].texels[3], 255);
}

TEST(Mips, SolidColour_StaysSolidInBothColourSpaces)
{
    for (const ColorSpace space : {ColorSpace::Linear, ColorSpace::Srgb})
    {
        const auto chain = GenerateMips(Solid(8, 4, 200, 100, 50, 128), space);
        ASSERT_EQ(chain.size(), 4u);
        for (const Image8& level : chain)
        {
            EXPECT_EQ(level.texels[0], 200);
            EXPECT_EQ(level.texels[1], 100);
            EXPECT_EQ(level.texels[2], 50);
            EXPECT_EQ(level.texels[3], 128);
        }
    }
}

TEST(Mips, SrgbAveragesInLinearLight)
{
    // Black and white average to linear 0.5, which is sRGB ~188, not 128.
    Image8 base{.width = 2, .height = 1, .texels = {0, 0, 0, 255, 255, 255, 255, 255}};
    const auto srgb = GenerateMips(base, ColorSpace::Srgb);
    EXPECT_NEAR(srgb[1].texels[0], 188, 1);
    EXPECT_EQ(srgb[1].texels[3], 255);
    const auto linear = GenerateMips(base, ColorSpace::Linear);
    EXPECT_EQ(linear[1].texels[0], 128);
}

TEST(Mips, OddSizes_ReachOneByOne)
{
    const auto chain = GenerateMips(Solid(5, 3, 1, 2, 3), ColorSpace::Linear);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[1].width, 2u);
    EXPECT_EQ(chain[1].height, 1u);
    EXPECT_EQ(chain[2].width, 1u);
}

TEST(Mips, FloatChain_AveragesExactly)
{
    ImageF base{.width = 2, .height = 2, .texels = {}};
    base.texels = {1, 2, 3, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const auto chain = GenerateMips(base);
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_FLOAT_EQ(chain[1].texels[0], 1.0f);
    EXPECT_FLOAT_EQ(chain[1].texels[1], 1.0f);
    EXPECT_FLOAT_EQ(chain[1].texels[3], 1.0f);
}

TEST(BuildTexture, Uncompressed_KeepsTexelsAndBuildsAFullChain)
{
    const Image8 image = Gradient(16, 8);
    auto texture = BuildTexture(image, {});
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ(texture->format, TextureFormat::Rgba8);
    EXPECT_EQ(texture->mips.size(), MipCount(16, 8));
    ASSERT_TRUE(ValidateTexture(*texture).has_value());
    auto level0 = DecodeMip(*texture, 0);
    ASSERT_TRUE(level0.has_value());
    EXPECT_EQ(level0->texels, image.texels);
}

TEST(BuildTexture, WithoutMips_HasOneLevel)
{
    auto texture = BuildTexture(Gradient(8, 8), {.colorSpace = ColorSpace::Linear, .generateMips = false});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->mips.size(), 1u);
    EXPECT_EQ(texture->colorSpace, ColorSpace::Linear);
}

TEST(BuildTexture, BadImages_AreRejected)
{
    EXPECT_FALSE(BuildTexture(Image8{}, {}).has_value());
    Image8 wrong = Solid(4, 4, 0, 0, 0);
    wrong.texels.pop_back();
    EXPECT_FALSE(BuildTexture(wrong, {}).has_value());
    EXPECT_FALSE(BuildTexture(Image8{.width = kMaxTextureDimension + 1, .height = 1, .texels = {}}, {}).has_value());
}

TEST(BlockCompression, Bc1_SolidColourDecodesWithin565Precision)
{
    auto texture =
        BuildTexture(Solid(8, 8, 200, 100, 50),
                     {.colorSpace = ColorSpace::Srgb, .generateMips = true, .compression = TextureCompression::Bc1});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->format, TextureFormat::Bc1);
    for (std::uint32_t mip = 0; mip < texture->mips.size(); ++mip)
    {
        auto decoded = DecodeMip(*texture, mip);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_LE(MaxChannelError(*decoded, Solid(decoded->width, decoded->height, 200, 100, 50), 3), 8) << mip;
    }
}

TEST(BlockCompression, Bc1_GradientErrorIsBounded)
{
    const Image8 image = Gradient(32, 32);
    auto texture = BuildTexture(
        image, {.colorSpace = ColorSpace::Srgb, .generateMips = false, .compression = TextureCompression::Bc1});
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->mips[0].size(), 8u * 8u * 8u);
    auto decoded = DecodeMip(*texture, 0);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_LE(MaxChannelError(*decoded, image, 3), 40);
}

TEST(BlockCompression, Bc1_TwoColourBlockKeepsBothColours)
{
    Image8 image{.width = 4, .height = 4, .texels = {}};
    for (int i = 0; i < 16; ++i)
    {
        if (i % 2 == 0)
        {
            image.texels.insert(image.texels.end(), {255, 0, 0, 255});
        }
        else
        {
            image.texels.insert(image.texels.end(), {0, 0, 255, 255});
        }
    }
    auto texture = BuildTexture(
        image, {.colorSpace = ColorSpace::Srgb, .generateMips = false, .compression = TextureCompression::Bc1});
    ASSERT_TRUE(texture.has_value());
    auto decoded = DecodeMip(*texture, 0);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_LE(MaxChannelError(*decoded, image, 3), 16);
}

TEST(BlockCompression, Bc4_EightValueBlockIsAccurate)
{
    const Image8 image = Gradient(16, 16);
    auto texture = BuildTexture(
        image, {.colorSpace = ColorSpace::Linear, .generateMips = false, .compression = TextureCompression::Bc4});
    ASSERT_TRUE(texture.has_value());
    auto decoded = DecodeMip(*texture, 0);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_LE(MaxChannelError(*decoded, image, 1), 20); // red channel only
    EXPECT_EQ(decoded->texels[1], 0);
}

TEST(BlockCompression, Bc5_KeepsRedAndGreen)
{
    const Image8 image = Gradient(12, 20); // not multiples of four: edge blocks replicate
    auto texture = BuildTexture(
        image, {.colorSpace = ColorSpace::Linear, .generateMips = true, .compression = TextureCompression::Bc5});
    ASSERT_TRUE(texture.has_value());
    ASSERT_TRUE(ValidateTexture(*texture).has_value());
    auto decoded = DecodeMip(*texture, 0);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->width, 12u);
    EXPECT_EQ(decoded->height, 20u);
    EXPECT_LE(MaxChannelError(*decoded, image, 2), 24);
}

TEST(BuildTexture, Hdr_StoresFloatsLosslessly)
{
    ImageF image{.width = 2, .height = 2, .texels = {}};
    image.texels = {0.5f, 1.5f, 8.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 2.0f, 2.0f, 1.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    auto texture = BuildTexture(image, true);
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->format, TextureFormat::Rgba32F);
    EXPECT_EQ(texture->colorSpace, ColorSpace::Linear);
    ASSERT_EQ(texture->mips.size(), 2u);
    float first[4];
    std::memcpy(first, texture->mips[0].data(), sizeof(first));
    EXPECT_EQ(first[2], 8.0f);
}

TEST(TextureAsset, WriteThenRead_RoundTrips)
{
    auto texture = BuildTexture(
        Gradient(9, 5), {.colorSpace = ColorSpace::Srgb, .generateMips = true, .compression = TextureCompression::Bc1});
    ASSERT_TRUE(texture.has_value());
    const std::vector<std::byte> bytes = WriteTextureAsset(*texture);
    auto read = ReadTextureAsset(bytes);
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, *texture);

    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("textures/t.itex", bytes).has_value());
    auto loaded = LoadTextureAsset(files, "textures/t.itex");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, *texture);
}

TEST(TextureAsset, FlippedByte_IsCorrupt)
{
    auto texture = BuildTexture(Gradient(8, 8), {});
    ASSERT_TRUE(texture.has_value());
    std::vector<std::byte> bytes = WriteTextureAsset(*texture);
    bytes[bytes.size() / 2] ^= std::byte{0x20};
    auto read = ReadTextureAsset(bytes);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, ErrorCode::Corrupt);
}

TEST(TextureAsset, WrongLevelSize_FailsValidation)
{
    auto texture = BuildTexture(Gradient(8, 8), {});
    ASSERT_TRUE(texture.has_value());
    texture->mips[1].pop_back();
    EXPECT_FALSE(ValidateTexture(*texture).has_value());
    EXPECT_FALSE(ReadTextureAsset(WriteTextureAsset(*texture)).has_value());
}

TEST(TextureAsset, MissingFile_IsNotFound)
{
    MemoryFileSystem files;
    auto loaded = LoadTextureAsset(files, "nope.itex");
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, ErrorCode::NotFound);
}

TEST(DecodeMip, LevelOutOfRange_IsInvalid)
{
    auto texture = BuildTexture(Gradient(4, 4), {.colorSpace = ColorSpace::Srgb, .generateMips = false});
    ASSERT_TRUE(texture.has_value());
    EXPECT_FALSE(DecodeMip(*texture, 1).has_value());
}
