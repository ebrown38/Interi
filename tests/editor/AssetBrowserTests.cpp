// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace interi::editor;

namespace
{

const std::vector<AssetEntry> kAssets = {
    {.path = "imported/props/crate.fbx/manifest.imodel.json", .kind = AssetKind::Model},
    {.path = "imported/props/crate.fbx/Cube.imesh", .kind = AssetKind::Mesh},
    {.path = "imported/tex/Crate_Albedo.png.itex", .kind = AssetKind::Texture},
    {.path = "imported/tex/rock.png.itex", .kind = AssetKind::Texture},
    {.path = "materials/crate.material.json", .kind = AssetKind::Material},
    {.path = "levels/a.scene.json", .kind = AssetKind::Scene},
};

} // namespace

TEST(AssetKinds, ByFileName)
{
    EXPECT_EQ(KindOfPath("a/b.imesh"), AssetKind::Mesh);
    EXPECT_EQ(KindOfPath("a/b.prefab.json"), AssetKind::Prefab);
    EXPECT_EQ(KindOfPath("a/b.scene.json"), AssetKind::Scene);
    EXPECT_EQ(KindOfPath("a/manifest.imodel.json"), AssetKind::Model);
    EXPECT_EQ(KindOfPath("a/b.png.itex"), AssetKind::Texture);
    EXPECT_EQ(KindOfPath("a/b.imat"), AssetKind::Material);
    EXPECT_EQ(KindOfPath("a/b.material.json"), AssetKind::Material);
    EXPECT_EQ(KindOfPath("a/b.iskel"), AssetKind::Skeleton);
    EXPECT_EQ(KindOfPath("a/b.ianim"), AssetKind::Clip);
    EXPECT_FALSE(KindOfPath("a/b.png").has_value());
    EXPECT_FALSE(KindOfPath("a/b.json").has_value());
    EXPECT_FALSE(KindOfPath("a/b.iskin").has_value());
}

TEST(AssetKinds, NamesRoundTripAndPlaceableKinds)
{
    for (std::size_t i = 0; i < kAssetKindCount; ++i)
    {
        const auto kind = static_cast<AssetKind>(i);
        EXPECT_EQ(ParseAssetKind(ToString(kind)), kind);
    }
    EXPECT_FALSE(ParseAssetKind("nope").has_value());
    EXPECT_TRUE(IsPlaceable(AssetKind::Mesh));
    EXPECT_TRUE(IsPlaceable(AssetKind::Prefab));
    EXPECT_TRUE(IsPlaceable(AssetKind::Model));
    EXPECT_FALSE(IsPlaceable(AssetKind::Scene));
    EXPECT_FALSE(IsPlaceable(AssetKind::Texture));
}

TEST(AssetSearch, EveryWordMustMatchCaseInsensitively)
{
    const auto crate = FilterAssets(kAssets, "CRATE");
    ASSERT_EQ(crate.size(), 4u);
    EXPECT_EQ(FilterAssets(kAssets, "crate albedo").size(), 1u);
    EXPECT_EQ(FilterAssets(kAssets, "albedo crate").size(), 1u);
    EXPECT_TRUE(FilterAssets(kAssets, "crate zebra").empty());
}

TEST(AssetSearch, KindFilter)
{
    const auto textures = FilterAssets(kAssets, "kind:texture");
    ASSERT_EQ(textures.size(), 2u);
    EXPECT_EQ(textures[0].kind, AssetKind::Texture);
    EXPECT_EQ(FilterAssets(kAssets, "crate kind:texture").size(), 1u);
    EXPECT_EQ(FilterAssets(kAssets, "KIND:Mesh").size(), 1u);
    EXPECT_TRUE(FilterAssets(kAssets, "kind:unicorn").empty());
}

TEST(AssetSearch, EmptyQuery_MatchesEverything)
{
    EXPECT_EQ(FilterAssets(kAssets, "").size(), kAssets.size());
    EXPECT_EQ(FilterAssets(kAssets, "   ").size(), kAssets.size());
}

TEST(AssetSearch, KeepsTheOrder)
{
    const auto result = FilterAssets(kAssets, "imported");
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].path, kAssets[0].path);
    EXPECT_EQ(result[3].path, kAssets[3].path);
}

TEST(AssetBrowser, ListsTheCookedKinds)
{
    const interi::testing::ScopedTempDir dir;
    const auto touch = [&](const std::string& path)
    {
        std::filesystem::create_directories((dir.Path() / path).parent_path());
        std::ofstream(dir.Path() / path) << "x";
    };
    touch("imported/m.fbx/manifest.imodel.json");
    touch("imported/m.fbx/Body.imesh");
    touch("imported/m.fbx/Body.iskin");
    touch("imported/m.fbx/rig.iskel");
    touch("imported/m.fbx/walk.ianim");
    touch("imported/m.fbx/Skin.imat");
    touch("imported/t.png.itex");
    touch("m.fbx");
    touch("m.fbx.meta");
    const auto assets = ListAssets(dir.Path());
    ASSERT_EQ(assets.size(), 6u);
    std::size_t kinds[kAssetKindCount] = {};
    for (const AssetEntry& entry : assets)
    {
        ++kinds[static_cast<std::size_t>(entry.kind)];
    }
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Model)], 1u);
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Mesh)], 1u);
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Skeleton)], 1u);
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Clip)], 1u);
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Material)], 1u);
    EXPECT_EQ(kinds[static_cast<std::size_t>(AssetKind::Texture)], 1u);
}
