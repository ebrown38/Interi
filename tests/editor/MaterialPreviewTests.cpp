// SPDX-License-Identifier: MIT
#include <interi/editor/MaterialPreview.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace interi;
using namespace interi::editor;

namespace
{

const std::uint8_t* Pixel(const assets::Image8& image, std::uint32_t x, std::uint32_t y)
{
    return &image.texels[(std::size_t{y} * image.width + x) * 4];
}

int Luma(const std::uint8_t* p)
{
    return (p[0] + p[1] + p[2]) / 3;
}

assets::PbrMaterial Plastic(Vec4 color)
{
    assets::PbrMaterial m;
    m.baseColor = color;
    m.metallic = 0.0f;
    m.roughness = 0.5f;
    return m;
}

} // namespace

TEST(MaterialPreview, OutsideTheSphereIsTransparent)
{
    const assets::Image8 image = RenderMaterialPreview(Plastic({0.5f, 0.5f, 0.5f, 1.0f}), 32);
    ASSERT_EQ(image.texels.size(), 32u * 32u * 4u);
    EXPECT_EQ(Pixel(image, 0, 0)[3], 0);
    EXPECT_EQ(Pixel(image, 31, 31)[3], 0);
    EXPECT_EQ(Pixel(image, 16, 16)[3], 255);
}

TEST(MaterialPreview, ZeroSize_IsEmpty)
{
    const assets::Image8 image = RenderMaterialPreview({}, 0);
    EXPECT_TRUE(image.texels.empty());
    EXPECT_EQ(image.width, 0u);
}

TEST(MaterialPreview, IsDeterministic)
{
    const assets::PbrMaterial m = Plastic({0.2f, 0.6f, 0.3f, 1.0f});
    EXPECT_EQ(RenderMaterialPreview(m, 24).texels, RenderMaterialPreview(m, 24).texels);
}

TEST(MaterialPreview, BaseColorTintsTheSphere)
{
    const assets::Image8 red = RenderMaterialPreview(Plastic({0.9f, 0.1f, 0.1f, 1.0f}), 32);
    const std::uint8_t* centre = Pixel(red, 16, 16);
    EXPECT_GT(centre[0], centre[1] + 40);
    EXPECT_GT(centre[0], centre[2] + 40);
    const assets::Image8 blue = RenderMaterialPreview(Plastic({0.1f, 0.1f, 0.9f, 1.0f}), 32);
    EXPECT_GT(Pixel(blue, 16, 16)[2], Pixel(blue, 16, 16)[0] + 40);
}

TEST(MaterialPreview, TheLitSideIsBrighterThanTheDarkSide)
{
    const assets::Image8 image = RenderMaterialPreview(Plastic({0.7f, 0.7f, 0.7f, 1.0f}), 64);
    // The light comes from the upper left: compare symmetric points near the rim.
    EXPECT_GT(Luma(Pixel(image, 14, 20)), Luma(Pixel(image, 50, 44)));
}

TEST(MaterialPreview, SmoothMaterialsHaveASharperHighlight)
{
    assets::PbrMaterial smooth = Plastic({0.2f, 0.2f, 0.2f, 1.0f});
    smooth.roughness = 0.1f;
    assets::PbrMaterial rough = smooth;
    rough.roughness = 1.0f;
    const auto brightest = [](const assets::Image8& image)
    {
        int best = 0;
        for (std::size_t i = 0; i < image.texels.size(); i += 4)
        {
            best = std::max(best, Luma(&image.texels[i]));
        }
        return best;
    };
    EXPECT_GT(brightest(RenderMaterialPreview(smooth, 64)), brightest(RenderMaterialPreview(rough, 64)) + 20);
}

TEST(MaterialPreview, MetalsTintTheirReflection)
{
    assets::PbrMaterial gold = Plastic({1.0f, 0.77f, 0.34f, 1.0f});
    gold.metallic = 1.0f;
    gold.roughness = 0.3f;
    const assets::Image8 image = RenderMaterialPreview(gold, 64);
    const std::uint8_t* p = Pixel(image, 28, 28);
    EXPECT_GT(p[0], p[2]); // warmer than blue
}

TEST(MaterialPreview, EmissiveAddsLight)
{
    assets::PbrMaterial dark = Plastic({0.0f, 0.0f, 0.0f, 1.0f});
    assets::PbrMaterial glowing = dark;
    glowing.emissive = Vec3(0.0f, 2.0f, 0.0f);
    const assets::Image8 a = RenderMaterialPreview(dark, 32);
    const assets::Image8 b = RenderMaterialPreview(glowing, 32);
    EXPECT_GT(Pixel(b, 16, 16)[1], Pixel(a, 16, 16)[1] + 60);
}

TEST(MaterialPreview, BlendMaterialsKeepTheirAlpha)
{
    assets::PbrMaterial glass = Plastic({0.5f, 0.5f, 0.5f, 0.25f});
    glass.alphaMode = assets::AlphaMode::Blend;
    const assets::Image8 image = RenderMaterialPreview(glass, 32);
    EXPECT_NEAR(Pixel(image, 16, 16)[3], 64, 1);
    assets::PbrMaterial opaque = glass;
    opaque.alphaMode = assets::AlphaMode::Opaque;
    EXPECT_EQ(Pixel(RenderMaterialPreview(opaque, 32), 16, 16)[3], 255);
}

TEST(MaterialPreview, TexturesMultiplyTheFactor)
{
    assets::PbrMaterial m = Plastic({1.0f, 1.0f, 1.0f, 1.0f});
    m.baseColorTexture.path = "tex.itex";
    const TextureSampler green = [](const assets::TextureRef&, float, float) { return Vec4(0.0f, 1.0f, 0.0f, 1.0f); };
    const assets::Image8 image = RenderMaterialPreview(m, 32, green);
    const std::uint8_t* centre = Pixel(image, 16, 16);
    EXPECT_GT(centre[1], centre[0] + 40);
    // A sampler is only asked for textures the material has.
    bool asked = false;
    const TextureSampler probe = [&](const assets::TextureRef&, float, float)
    {
        asked = true;
        return Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    };
    (void)RenderMaterialPreview(Plastic({1, 1, 1, 1}), 16, probe);
    EXPECT_FALSE(asked);
}
