// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/assets/Texture.h>
#include <interi/editor/Thumbnails.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Primitives.h>

#include <gtest/gtest.h>

#include <array>
#include <set>

using namespace interi;
using namespace interi::editor;

namespace
{

assets::TextureAsset RedGreenTexture()
{
    assets::Image8 image{.width = 8, .height = 4, .texels = {}};
    for (std::uint32_t y = 0; y < 4; ++y)
    {
        for (std::uint32_t x = 0; x < 8; ++x)
        {
            const std::array<std::uint8_t, 4> texel =
                x < 4 ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{0, 255, 0, 255};
            image.texels.insert(image.texels.end(), texel.begin(), texel.end());
        }
    }
    return *assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Linear, .generateMips = true});
}

scene::TriangleMesh Box()
{
    scene::PrimitiveParams box;
    box.shape = scene::PrimitiveShape::Box;
    return *scene::GeneratePrimitive(box);
}

void Write(MemoryFileSystem& files, const std::string& path, const std::vector<std::byte>& bytes)
{
    ASSERT_TRUE(files.WriteFile(path, bytes).has_value());
}

const std::uint8_t* At(const Thumbnail& t, std::uint32_t x, std::uint32_t y)
{
    return &t.rgba[(std::size_t{y} * t.size + x) * 4];
}

std::size_t Covered(const Thumbnail& t)
{
    std::size_t count = 0;
    for (std::size_t i = 3; i < t.rgba.size(); i += 4)
    {
        count += t.rgba[i] != 0 ? 1 : 0;
    }
    return count;
}

} // namespace

TEST(Thumbnails, TextureKeepsItsAspectRatio)
{
    const Thumbnail thumbnail = TextureThumbnail(RedGreenTexture(), 16); // 8x4 -> 16x8 centred
    ASSERT_EQ(thumbnail.rgba.size(), 16u * 16u * 4u);
    EXPECT_EQ(At(thumbnail, 2, 0)[3], 0); // letterbox above
    EXPECT_EQ(At(thumbnail, 2, 15)[3], 0);
    EXPECT_EQ(At(thumbnail, 2, 8)[0], 255);  // left half red
    EXPECT_EQ(At(thumbnail, 13, 8)[1], 255); // right half green
    EXPECT_EQ(Covered(thumbnail), 16u * 8u);
}

TEST(Thumbnails, SrgbMeansTheTexelsAreShownAsStored)
{
    assets::Image8 image{.width = 1, .height = 1, .texels = {128, 128, 128, 255}};
    const auto texture = *assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Srgb, .generateMips = false});
    const Thumbnail thumbnail = TextureThumbnail(texture, 4);
    EXPECT_EQ(At(thumbnail, 2, 2)[0], 128);
}

TEST(Thumbnails, MeshIsShadedAndFramed)
{
    const Thumbnail thumbnail = MeshThumbnail(Box(), 32);
    const std::size_t covered = Covered(thumbnail);
    EXPECT_GT(covered, 32u * 32u / 5);
    EXPECT_LT(covered, 32u * 32u);
    EXPECT_EQ(At(thumbnail, 0, 0)[3], 0); // corners stay empty
    // Different faces get different brightness.
    std::set<int> shades;
    for (std::uint32_t y = 0; y < 32; ++y)
    {
        for (std::uint32_t x = 0; x < 32; ++x)
        {
            if (At(thumbnail, x, y)[3] != 0)
            {
                shades.insert(At(thumbnail, x, y)[0] / 8);
            }
        }
    }
    EXPECT_GE(shades.size(), 2u);
}

TEST(Thumbnails, EmptyMeshAndZeroSizeAreEmpty)
{
    EXPECT_EQ(Covered(MeshThumbnail({}, 16)), 0u);
    EXPECT_TRUE(MeshThumbnail(Box(), 0).rgba.empty());
    EXPECT_TRUE(TextureThumbnail(assets::TextureAsset{}, 8).rgba.size() == 8u * 8u * 4u);
}

TEST(Thumbnails, MakeThumbnail_ByKind)
{
    MemoryFileSystem files;
    Write(files, "t.itex", assets::WriteTextureAsset(RedGreenTexture()));
    Write(files, "box.imesh", scene::WriteMeshAsset(Box()));
    assets::PbrMaterial material;
    material.baseColor = Vec4(0.2f, 0.8f, 0.2f, 1.0f);
    Write(files, "m.imat", assets::WriteMaterialAsset(material));
    const std::string json = assets::WriteMaterial(material);
    ASSERT_TRUE(
        files.WriteFile("m.material.json", {reinterpret_cast<const std::byte*>(json.data()), json.size()}).has_value());
    asset_import::ModelManifest manifest;
    manifest.meshes.push_back({.name = "Box", .mesh = "box.imesh", .skin = {}, .materials = {}});
    const std::string manifestText = asset_import::WriteModelManifest(manifest);
    ASSERT_TRUE(files
                    .WriteFile("manifest.imodel.json",
                               {reinterpret_cast<const std::byte*>(manifestText.data()), manifestText.size()})
                    .has_value());

    for (const AssetEntry& entry :
         {AssetEntry{"t.itex", AssetKind::Texture}, AssetEntry{"box.imesh", AssetKind::Mesh},
          AssetEntry{"m.imat", AssetKind::Material}, AssetEntry{"m.material.json", AssetKind::Material},
          AssetEntry{"manifest.imodel.json", AssetKind::Model}})
    {
        auto thumbnail = MakeThumbnail(files, entry, 24);
        ASSERT_TRUE(thumbnail.has_value()) << entry.path << ": " << thumbnail.error().message;
        EXPECT_EQ(thumbnail->size, 24u);
        EXPECT_GT(Covered(*thumbnail), 0u) << entry.path;
    }
    const auto green = *MakeThumbnail(files, {"m.imat", AssetKind::Material}, 24);
    EXPECT_GT(At(green, 12, 12)[1], At(green, 12, 12)[0]);
}

TEST(Thumbnails, MaterialShowsItsCookedTexture)
{
    MemoryFileSystem files;
    assets::Image8 image{.width = 2, .height = 2, .texels = {}};
    for (int i = 0; i < 4; ++i)
    {
        image.texels.insert(image.texels.end(), {255, 0, 0, 255});
    }
    Write(files, "red.itex",
          assets::WriteTextureAsset(
              *assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Srgb, .generateMips = true})));
    assets::PbrMaterial material;
    material.metallic = 0.0f;
    material.baseColorTexture.path = "red.itex";
    Write(files, "m.imat", assets::WriteMaterialAsset(material));
    const auto thumbnail = MakeThumbnail(files, {"m.imat", AssetKind::Material}, 32);
    ASSERT_TRUE(thumbnail.has_value());
    EXPECT_GT(At(*thumbnail, 16, 16)[0], At(*thumbnail, 16, 16)[1] + 40);
}

TEST(Thumbnails, UnsupportedKindsAndBrokenFiles)
{
    MemoryFileSystem files;
    EXPECT_EQ(MakeThumbnail(files, {"a.scene.json", AssetKind::Scene}, 16).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(MakeThumbnail(files, {"missing.itex", AssetKind::Texture}, 16).error().code, ErrorCode::NotFound);
    Write(files, "broken.imesh", {std::byte{1}, std::byte{2}});
    EXPECT_FALSE(MakeThumbnail(files, {"broken.imesh", AssetKind::Mesh}, 16).has_value());
}

TEST(ThumbnailCache, MakesOnceAndRemembersFailures)
{
    MemoryFileSystem files;
    Write(files, "box.imesh", scene::WriteMeshAsset(Box()));
    ThumbnailCache cache(16);
    const AssetEntry good{"box.imesh", AssetKind::Mesh};
    const AssetEntry bad{"nope.imesh", AssetKind::Mesh};
    const Thumbnail* first = cache.Get(files, good);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->size, 16u);
    EXPECT_EQ(cache.Get(files, good), first); // same object: not rebuilt
    EXPECT_EQ(cache.Get(files, bad), nullptr);
    EXPECT_EQ(cache.Count(), 2u);
    // The file appears later; the failure sticks until invalidated.
    Write(files, "nope.imesh", scene::WriteMeshAsset(Box()));
    EXPECT_EQ(cache.Get(files, bad), nullptr);
    cache.Invalidate("nope.imesh");
    EXPECT_NE(cache.Get(files, bad), nullptr);
    cache.Clear();
    EXPECT_EQ(cache.Count(), 0u);
}

TEST(TextureSampler, ReadsCookedAndSourceNamedTextures)
{
    MemoryFileSystem files;
    assets::Image8 image{.width = 1, .height = 1, .texels = {0, 255, 0, 255}};
    Write(files, "imported/tex/g.png.itex",
          assets::WriteTextureAsset(
              *assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Linear, .generateMips = false})));
    const TextureSampler sampler = MakeTextureSampler(files);
    assets::TextureRef cooked;
    cooked.path = "imported/tex/g.png.itex";
    assets::TextureRef source;
    source.path = "tex/g.png"; // as authored
    assets::TextureRef missing;
    missing.path = "tex/none.png";
    EXPECT_EQ(sampler(cooked, 0.5f, 0.5f), Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(sampler(source, 0.5f, 0.5f), Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    EXPECT_EQ(sampler(missing, 0.5f, 0.5f), Vec4(1.0f, 1.0f, 1.0f, 1.0f));
}
