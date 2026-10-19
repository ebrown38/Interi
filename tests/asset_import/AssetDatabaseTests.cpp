// SPDX-License-Identifier: MIT
#include <interi/asset_import/AssetDatabase.h>
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/assets/Skeleton.h>
#include <interi/assets/Texture.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>
#include <interi/testing/ScopedTempDir.h>

#include "ImportFixtures.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>

using namespace interi;
using namespace interi::asset_import;
using namespace interi::testing_fixtures;

namespace
{

void WriteFile(const std::filesystem::path& root, const std::string& path, std::span<const std::byte> bytes)
{
    const std::filesystem::path full = root / path;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteText(const std::filesystem::path& root, const std::string& path, const std::string& text)
{
    WriteFile(root, path, Bytes(text));
}

std::string ReadText(const std::filesystem::path& root, const std::string& path)
{
    std::ifstream in(root / path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> Solid(std::uint8_t value)
{
    std::vector<std::uint8_t> pixels;
    for (int i = 0; i < 16; ++i)
    {
        pixels.insert(pixels.end(), {value, value, value, 255});
    }
    return pixels;
}

std::vector<std::byte> SolidPng(std::uint8_t value)
{
    return MakePng(4, 4, Solid(value));
}

/// Deterministic guids: 1, 2, 3...
assets::GuidGenerator Counter()
{
    auto next = std::make_shared<std::uint64_t>(0);
    return [next] { return assets::Guid{.high = 0xABCD, .low = ++*next}; };
}

class AssetDatabaseTest : public ::testing::Test
{
protected:
    AssetDatabase Make()
    {
        return AssetDatabase({.root = m_dir.Path(), .importedFolder = "imported", .guids = Counter()});
    }
    const std::filesystem::path& Root() const { return m_dir.Path(); }

    static bool Contains(const std::vector<std::string>& list, std::string_view value)
    {
        return std::ranges::find(list, value) != list.end();
    }

private:
    interi::testing::ScopedTempDir m_dir;
};

} // namespace

TEST_F(AssetDatabaseTest, Scan_CreatesMetaFilesWithGuids)
{
    WriteFile(Root(), "tex/a.png", SolidPng(10));
    WriteText(Root(), "models/cube.obj", MakeCubeObj());
    WriteText(Root(), "mat/m.material.json", "{}");
    WriteText(Root(), "notes.txt", "ignored");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.Records().size(), 3u);
    EXPECT_EQ(db.Records()[0].path, "mat/m.material.json"); // sorted
    EXPECT_EQ(db.Records()[0].kind, SourceKind::Material);
    EXPECT_EQ(db.Records()[1].kind, SourceKind::Model);
    EXPECT_EQ(db.Records()[2].kind, SourceKind::Texture);
    for (const AssetRecord& record : db.Records())
    {
        EXPECT_FALSE(record.guid.IsNull());
        EXPECT_TRUE(std::filesystem::exists(Root() / (record.path + ".meta"))) << record.path;
        EXPECT_EQ(db.Find(record.guid), &record);
        EXPECT_EQ(db.FindByPath(record.path), &record);
    }
    EXPECT_FALSE(std::filesystem::exists(Root() / "notes.txt.meta"));
}

TEST_F(AssetDatabaseTest, Scan_KeepsGuidsAcrossRescansAndInstances)
{
    WriteFile(Root(), "a.png", SolidPng(1));
    AssetDatabase first = Make();
    ASSERT_TRUE(first.Scan().has_value());
    const assets::Guid guid = first.Records()[0].guid;
    ASSERT_TRUE(first.Scan().has_value());
    EXPECT_EQ(first.Records()[0].guid, guid);
    AssetDatabase second = Make(); // a fresh counter would hand out the same first guid; the .meta wins anyway
    WriteFile(Root(), "b.png", SolidPng(2));
    ASSERT_TRUE(second.Scan().has_value());
    EXPECT_EQ(second.FindByPath("a.png")->guid, guid);
    EXPECT_NE(second.FindByPath("b.png")->guid, guid);
}

TEST_F(AssetDatabaseTest, Scan_RenamedPairKeepsItsGuid)
{
    WriteFile(Root(), "old.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const assets::Guid guid = db.FindByPath("old.png")->guid;
    std::filesystem::rename(Root() / "old.png", Root() / "new.png");
    std::filesystem::rename(Root() / "old.png.meta", Root() / "new.png.meta");
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.Records().size(), 1u);
    EXPECT_EQ(db.Records()[0].path, "new.png");
    EXPECT_EQ(db.Records()[0].guid, guid);
}

TEST_F(AssetDatabaseTest, Scan_DuplicatedMetaGetsANewGuid)
{
    WriteFile(Root(), "a.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    std::filesystem::copy_file(Root() / "a.png", Root() / "b.png");
    std::filesystem::copy_file(Root() / "a.png.meta", Root() / "b.png.meta");
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.Records().size(), 2u);
    EXPECT_NE(db.Records()[0].guid, db.Records()[1].guid);
    EXPECT_FALSE(db.ScanWarnings().empty());
}

TEST_F(AssetDatabaseTest, Scan_CorruptMetaIsReplacedWithAWarning)
{
    WriteFile(Root(), "a.png", SolidPng(1));
    WriteText(Root(), "a.png.meta", "{ not json");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.Records().size(), 1u);
    EXPECT_FALSE(db.Records()[0].guid.IsNull());
    EXPECT_FALSE(db.ScanWarnings().empty());
    AssetDatabase again = Make();
    ASSERT_TRUE(again.Scan().has_value());
    EXPECT_EQ(again.Records()[0].guid, db.Records()[0].guid);
    EXPECT_TRUE(again.ScanWarnings().empty());
}

TEST_F(AssetDatabaseTest, Scan_SkipsTheImportedFolder)
{
    WriteFile(Root(), "a.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.Cook("a.png").has_value());
    WriteFile(Root(), "imported/stray.png", SolidPng(3));
    ASSERT_TRUE(db.Scan().has_value());
    EXPECT_EQ(db.Records().size(), 1u);
}

TEST_F(AssetDatabaseTest, CookTexture_WritesAValidAssetWithMips)
{
    WriteFile(Root(), "tex/a.png", SolidPng(10));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport report = db.CookAll();
    EXPECT_EQ(report.cooked, (std::vector<std::string>{"tex/a.png"}));
    EXPECT_TRUE(report.failed.empty());
    NativeFileSystem files(Root());
    auto texture = assets::LoadTextureAsset(files, "imported/tex/a.png.itex");
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ(texture->width, 4u);
    EXPECT_EQ(texture->mips.size(), 3u);
    const AssetRecord* record = db.FindByPath("tex/a.png");
    ASSERT_EQ(record->products.size(), 1u);
    EXPECT_EQ(record->products[0].guid, record->guid);
    EXPECT_NE(record->cookKey, 0u);
}

TEST_F(AssetDatabaseTest, SecondCook_SkipsEverything)
{
    WriteFile(Root(), "tex/a.png", SolidPng(10));
    WriteText(Root(), "models/cube.obj", MakeCubeObj());
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    EXPECT_EQ(db.CookAll().cooked.size(), 2u);
    const CookReport second = db.CookAll();
    EXPECT_TRUE(second.cooked.empty());
    EXPECT_EQ(second.skipped.size(), 2u);
    // Also across database instances: the key lives in the .meta.
    AssetDatabase other = Make();
    ASSERT_TRUE(other.Scan().has_value());
    EXPECT_TRUE(other.CookAll().cooked.empty());
}

TEST_F(AssetDatabaseTest, ChangedSource_ReimportsOnlyThatSource)
{
    WriteFile(Root(), "a.png", SolidPng(10));
    WriteFile(Root(), "b.png", SolidPng(20));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.CookAll().cooked.size(), 2u);
    const std::uint64_t keyB = db.FindByPath("b.png")->cookKey;
    WriteFile(Root(), "a.png", SolidPng(11)); // same size, different content
    const CookReport report = db.CookAll();
    EXPECT_EQ(report.cooked, (std::vector<std::string>{"a.png"}));
    EXPECT_EQ(report.skipped, (std::vector<std::string>{"b.png"}));
    EXPECT_EQ(db.FindByPath("b.png")->cookKey, keyB);
}

TEST_F(AssetDatabaseTest, ChangedSettings_Reimport)
{
    WriteText(Root(), "models/cube.obj", MakeCubeObj());
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.CookAll().cooked.size(), 1u);
    NativeFileSystem files(Root());
    auto before = scene::LoadMeshAsset(files, "imported/models/cube.obj/mesh.imesh");
    ASSERT_TRUE(before.has_value()) << before.error().message;
    ModelImportSettings settings;
    settings.scale = 2.0f;
    ASSERT_TRUE(db.SetModelSettings("models/cube.obj", settings).has_value());
    EXPECT_EQ(db.CookAll().cooked.size(), 1u);
    auto after = scene::LoadMeshAsset(files, "imported/models/cube.obj/mesh.imesh");
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(scene::MeshVolume(*after), 8.0 * scene::MeshVolume(*before), 1e-3);
    // The settings are in the .meta, so another instance sees them.
    AssetDatabase other = Make();
    ASSERT_TRUE(other.Scan().has_value());
    EXPECT_EQ(other.FindByPath("models/cube.obj")->model, settings);
    EXPECT_TRUE(other.CookAll().cooked.empty());
}

TEST_F(AssetDatabaseTest, SetSettings_RejectsBadInput)
{
    WriteText(Root(), "models/cube.obj", MakeCubeObj());
    WriteFile(Root(), "a.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ModelImportSettings bad;
    bad.scale = -1.0f;
    EXPECT_FALSE(db.SetModelSettings("models/cube.obj", bad).has_value());
    EXPECT_FALSE(db.SetModelSettings("a.png", {}).has_value());
    EXPECT_FALSE(db.SetTextureSettings("missing.png", {}).has_value());
}

TEST_F(AssetDatabaseTest, DamagedOutput_IsCookedAgain)
{
    WriteFile(Root(), "a.png", SolidPng(10));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.CookAll().cooked.size(), 1u);
    WriteText(Root(), "imported/a.png.itex", "garbage");
    EXPECT_EQ(db.CookAll().cooked.size(), 1u);
    NativeFileSystem files(Root());
    EXPECT_TRUE(assets::LoadTextureAsset(files, "imported/a.png.itex").has_value());
    std::filesystem::remove(Root() / "imported/a.png.itex");
    EXPECT_EQ(db.CookAll().cooked.size(), 1u);
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/a.png.itex"));
}

TEST_F(AssetDatabaseTest, FailedImport_IsReportedAndOthersStillCook)
{
    WriteText(Root(), "bad.png", "not a png");
    WriteFile(Root(), "good.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport report = db.CookAll();
    ASSERT_EQ(report.failed.size(), 1u);
    EXPECT_TRUE(report.failed[0].starts_with("bad.png:"));
    EXPECT_EQ(report.cooked, (std::vector<std::string>{"good.png"}));
    EXPECT_EQ(db.FindByPath("bad.png")->cookKey, 0u);
    EXPECT_EQ(db.CookAll().failed.size(), 1u); // it keeps failing until the source changes
}

TEST_F(AssetDatabaseTest, Cook_UnknownPath_IsNotFound)
{
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    auto result = db.Cook("nope.png");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(AssetDatabaseTest, Model_ProducesMeshesMaterialsAndAManifest)
{
    WriteText(Root(), "models/m.mtl", "newmtl Red\nKd 1 0 0\nnewmtl Blue\nKd 0 0 1\n");
    WriteText(Root(), "models/two.obj",
              "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nusemtl Red\nf 1 2 3\nusemtl Blue\nf 2 4 3\n");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport report = db.CookAll();
    ASSERT_TRUE(report.failed.empty()) << report.failed[0];
    const AssetRecord* record = db.FindByPath("models/two.obj");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->reads, (std::vector<std::string>{"models/m.mtl"}));
    NativeFileSystem files(Root());
    auto manifest = ParseModelManifest(ReadText(Root(), "imported/models/two.obj/manifest.imodel.json"));
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;
    ASSERT_EQ(manifest->meshes.size(), 1u);
    EXPECT_EQ(manifest->source, "models/two.obj");
    EXPECT_EQ(manifest->materials.size(), 2u);
    ASSERT_EQ(manifest->meshes[0].materials.size(), 2u);
    auto red = assets::LoadMaterialAsset(files, manifest->meshes[0].materials[0]);
    auto blue = assets::LoadMaterialAsset(files, manifest->meshes[0].materials[1]);
    ASSERT_TRUE(red.has_value());
    ASSERT_TRUE(blue.has_value());
    EXPECT_EQ(red->baseColor, Vec4(1, 0, 0, 1));
    EXPECT_EQ(blue->baseColor, Vec4(0, 0, 1, 1));
    auto mesh = scene::LoadMeshAsset(files, manifest->meshes[0].mesh);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->TriangleCount(), 2u);
    // Every product is on disk with the recorded hash.
    for (const CookedProduct& product : record->products)
    {
        const auto bytes = files.ReadFile(product.path);
        ASSERT_TRUE(bytes.has_value()) << product.path;
        EXPECT_EQ(HashBytes(bytes->data(), bytes->size()), product.hash);
    }
}

TEST_F(AssetDatabaseTest, SubAssetGuids_AreStableAcrossReimports)
{
    WriteText(Root(), "cube.obj", MakeCubeObj());
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_EQ(db.CookAll().cooked.size(), 1u);
    const std::vector<CookedProduct> first = db.FindByPath("cube.obj")->products;
    WriteText(Root(), "cube.obj", MakeCubeObj() + "# changed\n");
    ASSERT_EQ(db.CookAll().cooked.size(), 1u);
    const std::vector<CookedProduct> second = db.FindByPath("cube.obj")->products;
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(first[i].guid, second[i].guid) << first[i].name;
        EXPECT_FALSE(first[i].guid.IsNull());
    }
    const assets::Guid parent = db.FindByPath("cube.obj")->guid;
    EXPECT_EQ(DeriveGuid(parent, "mesh", "x"), DeriveGuid(parent, "mesh", "x"));
    EXPECT_NE(DeriveGuid(parent, "mesh", "x"), DeriveGuid(parent, "mesh", "y"));
    EXPECT_NE(DeriveGuid(parent, "mesh", "x"), DeriveGuid(assets::Guid{1, 1}, "mesh", "x"));
}

TEST_F(AssetDatabaseTest, ExternalBuffer_IsADependencyAndChangesReimport)
{
    GltfOptions options;
    options.externalBuffer = true;
    GltfFile file = MakeCubeGltf(options);
    WriteFile(Root(), "models/cube.gltf", file.main);
    WriteFile(Root(), "models/cube.bin", file.bin);
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    EXPECT_EQ(db.Dependencies("models/cube.gltf"), (std::vector<std::string>{"models/cube.bin"}));
    EXPECT_EQ(db.Dependents("models/cube.bin"), (std::vector<std::string>{"models/cube.gltf"}));
    EXPECT_TRUE(db.CookAll().cooked.empty());
    // Move one vertex: same size, different content.
    std::vector<std::byte> edited = file.bin;
    edited[100] ^= std::byte{0x01};
    WriteFile(Root(), "models/cube.bin", edited);
    const CookReport report = db.CookAll();
    EXPECT_EQ(report.cooked, (std::vector<std::string>{"models/cube.gltf"}));
}

TEST_F(AssetDatabaseTest, MaterialTextures_ResolveToCookedTexturesAndTrackGuids)
{
    WriteText(
        Root(), "mat/m.material.json",
        R"json({"name": "M", "textures": {"baseColor": {"path": "tex/a.png"}, "normal": {"path": "tex/missing.png"}}})json");
    WriteFile(Root(), "tex/a.png", SolidPng(10));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport report = db.CookAll();
    ASSERT_TRUE(report.failed.empty());
    EXPECT_FALSE(report.warnings.empty()); // the missing texture
    NativeFileSystem files(Root());
    auto material = assets::LoadMaterialAsset(files, "imported/mat/m.material.json.imat");
    ASSERT_TRUE(material.has_value()) << material.error().message;
    EXPECT_EQ(material->baseColorTexture.path, "imported/tex/a.png.itex");
    EXPECT_EQ(material->baseColorTexture.guid, db.FindByPath("tex/a.png")->guid);
    EXPECT_EQ(material->normalTexture.path, "tex/missing.png");
    EXPECT_TRUE(material->normalTexture.guid.IsNull());
    EXPECT_EQ(db.Dependents("tex/a.png"), (std::vector<std::string>{"mat/m.material.json"}));
    EXPECT_EQ(db.Dependencies("mat/m.material.json"), (std::vector<std::string>{"tex/a.png", "tex/missing.png"}));
    // A texture that appears later is picked up: the material's key includes the guids it refers to.
    EXPECT_TRUE(db.CookAll().cooked.empty());
    WriteFile(Root(), "tex/missing.png", SolidPng(30));
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport again = db.CookAll();
    EXPECT_TRUE(Contains(again.cooked, "mat/m.material.json"));
    EXPECT_TRUE(Contains(again.cooked, "tex/missing.png"));
    auto updated = assets::LoadMaterialAsset(files, "imported/mat/m.material.json.imat");
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->normalTexture.path, "imported/tex/missing.png.itex");
}

TEST_F(AssetDatabaseTest, TransitiveDependents_FollowTheChain)
{
    WriteFile(Root(), "tex/a.png", SolidPng(10));
    WriteText(Root(), "m.material.json", R"json({"textures": {"baseColor": {"path": "tex/a.png"}}})json");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    EXPECT_EQ(db.Dependents("tex/a.png", true), (std::vector<std::string>{"m.material.json"}));
    EXPECT_TRUE(db.Dependents("m.material.json", true).empty());
}

TEST_F(AssetDatabaseTest, Glb_EmbeddedImagesBecomeTextureAssets)
{
    // A GLB with a base colour texture inside the file.
    GltfOptions options;
    options.glb = true;
    options.materials = nlohmann::json::array(
        {{{"name", "Painted"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
    options.extraRoot = {
        {"images", nlohmann::json::array({nlohmann::json{{"mimeType", "image/png"}, {"bufferView", 4}}})},
        {"textures", nlohmann::json::array({nlohmann::json{{"source", 0}}})}};
    GltfFile file = MakeCubeGltf(options);
    const auto png = SolidPng(200);
    std::uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, file.main.data() + 12, 4);
    nlohmann::json doc =
        nlohmann::json::parse(std::string(reinterpret_cast<const char*>(file.main.data() + 20), jsonLength));
    std::vector<std::byte> bin(file.main.begin() + 20 + jsonLength + 8, file.main.end());
    doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", png.size()}});
    bin.insert(bin.end(), png.begin(), png.end());
    while (bin.size() % 4 != 0)
    {
        bin.push_back(std::byte{0});
    }
    doc["buffers"][0]["byteLength"] = bin.size();
    std::string text = doc.dump();
    while (text.size() % 4 != 0)
    {
        text.push_back(' ');
    }
    std::vector<std::byte> glb;
    Append(glb, std::uint32_t{0x46546C67});
    Append(glb, std::uint32_t{2});
    Append(glb, static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
    Append(glb, static_cast<std::uint32_t>(text.size()));
    Append(glb, std::uint32_t{0x4E4F534A});
    const auto textBytes = Bytes(text);
    glb.insert(glb.end(), textBytes.begin(), textBytes.end());
    Append(glb, static_cast<std::uint32_t>(bin.size()));
    Append(glb, std::uint32_t{0x004E4942});
    glb.insert(glb.end(), bin.begin(), bin.end());
    WriteFile(Root(), "props/painted.glb", glb);

    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    const CookReport report = db.CookAll();
    ASSERT_TRUE(report.failed.empty()) << report.failed[0];
    const AssetRecord* record = db.FindByPath("props/painted.glb");
    ASSERT_NE(record, nullptr);
    NativeFileSystem files(Root());
    auto manifest = ParseModelManifest(ReadText(Root(), "imported/props/painted.glb/manifest.imodel.json"));
    ASSERT_TRUE(manifest.has_value());
    ASSERT_EQ(manifest->textures.size(), 1u);
    auto texture = assets::LoadTextureAsset(files, manifest->textures[0]);
    ASSERT_TRUE(texture.has_value()) << texture.error().message;
    EXPECT_EQ(texture->colorSpace, assets::ColorSpace::Srgb);
    auto material = assets::LoadMaterialAsset(files, manifest->materials[0]);
    ASSERT_TRUE(material.has_value());
    EXPECT_EQ(material->baseColorTexture.path, manifest->textures[0]);
    EXPECT_FALSE(material->baseColorTexture.guid.IsNull());
}

TEST_F(AssetDatabaseTest, Prune_RemovesOrphansAndTheirOutput)
{
    WriteText(Root(), "cube.obj", MakeCubeObj());
    WriteFile(Root(), "keep.png", SolidPng(1));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    ASSERT_TRUE(std::filesystem::exists(Root() / "imported/cube.obj/mesh.imesh"));
    std::filesystem::remove(Root() / "cube.obj");
    const std::vector<std::string> removed = db.Prune();
    EXPECT_TRUE(Contains(removed, "cube.obj.meta"));
    EXPECT_TRUE(Contains(removed, "imported/cube.obj/mesh.imesh"));
    EXPECT_FALSE(std::filesystem::exists(Root() / "cube.obj.meta"));
    EXPECT_FALSE(std::filesystem::exists(Root() / "imported/cube.obj"));
    EXPECT_TRUE(std::filesystem::exists(Root() / "keep.png.meta"));
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/keep.png.itex"));
    ASSERT_TRUE(db.Scan().has_value());
    EXPECT_EQ(db.Records().size(), 1u);
}

TEST_F(AssetDatabaseTest, ReimportDropsProductsTheSourceNoLongerHas)
{
    WriteText(Root(), "two.obj", "o A\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no B\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/two.obj/B.imesh"));
    WriteText(Root(), "two.obj", "o A\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    ASSERT_TRUE(db.CookAll().failed.empty());
    EXPECT_FALSE(std::filesystem::exists(Root() / "imported/two.obj/B.imesh"));
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/two.obj/A.imesh"));
}

TEST_F(AssetDatabaseTest, DuplicateNames_GetSuffixes)
{
    WriteText(Root(), "dup.obj",
              "o Part\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no Part\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n");
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/dup.obj/Part.imesh"));
    EXPECT_TRUE(std::filesystem::exists(Root() / "imported/dup.obj/Part_2.imesh"));
}

TEST_F(AssetDatabaseTest, Watcher_ReimportsWhenASourceChanges)
{
    WriteFile(Root(), "a.png", SolidPng(10));
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    ASSERT_TRUE(db.CookAll().failed.empty());
    AssetWatcher watcher(db);
    EXPECT_FALSE(watcher.Update().changed); // records the state
    EXPECT_FALSE(watcher.Update().changed);
    WriteFile(Root(), "a.png", MakePng(8, 8, std::vector<std::uint8_t>(8 * 8 * 4, 99))); // a different size
    const auto outcome = watcher.Update();
    EXPECT_TRUE(outcome.changed);
    EXPECT_EQ(outcome.report.cooked, (std::vector<std::string>{"a.png"}));
    NativeFileSystem files(Root());
    auto texture = assets::LoadTextureAsset(files, "imported/a.png.itex");
    ASSERT_TRUE(texture.has_value());
    EXPECT_EQ(texture->width, 8u);
    EXPECT_FALSE(watcher.Update().changed); // its own writes don't count
}

TEST_F(AssetDatabaseTest, Watcher_PicksUpNewSources)
{
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    AssetWatcher watcher(db);
    EXPECT_FALSE(watcher.Update().changed);
    WriteText(Root(), "new.obj", MakeCubeObj());
    const auto outcome = watcher.Update();
    EXPECT_TRUE(outcome.changed);
    EXPECT_EQ(outcome.report.cooked, (std::vector<std::string>{"new.obj"}));
    EXPECT_EQ(db.Records().size(), 1u);
}

TEST_F(AssetDatabaseTest, FindByProduct_NamesTheSource)
{
    WriteText(Root(), "models/cube.obj", MakeCubeObj());
    AssetDatabase db = Make();
    ASSERT_TRUE(db.Scan().has_value());
    EXPECT_EQ(db.FindByProduct("imported/models/cube.obj/mesh.imesh"), nullptr); // not cooked yet
    ASSERT_TRUE(db.CookAll().failed.empty());
    const AssetRecord* record = db.FindByProduct("imported/models/cube.obj/mesh.imesh");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->path, "models/cube.obj");
    EXPECT_EQ(db.FindByProduct("imported/nothing.imesh"), nullptr);
}

namespace
{

// Cooks a.png, then points the product path in a.png.meta at `hostile` (a file that must survive).
void PoisonProductPath(const std::filesystem::path& root, const std::string& hostile)
{
    std::string meta = ReadText(root, "a.png.meta");
    const std::string product = "imported/a.png.itex";
    const auto at = meta.find(product);
    ASSERT_NE(at, std::string::npos) << meta;
    meta.replace(at, product.size(), hostile);
    WriteText(root, "a.png.meta", meta);
}

} // namespace

TEST_F(AssetDatabaseTest, Cook_MetaProductsOutsideImported_NothingDeleted)
{
    interi::testing::ScopedTempDir outside;
    WriteText(outside.Path(), "victim.txt", "keep");
    WriteText(Root(), "keep/inside.txt", "keep");
    const std::string absolute = (outside.Path() / "victim.txt").generic_string();
    // The traversal path names the same victim file from inside the root.
    const std::string traversal = "../" + outside.Path().filename().string() + "/victim.txt";
    for (const std::string& hostile :
         {absolute, traversal, std::string("imported/../keep/inside.txt"), std::string("keep/inside.txt")})
    {
        WriteFile(Root(), "a.png", SolidPng(10));
        std::filesystem::remove(Root() / "a.png.meta");
        {
            AssetDatabase db = Make();
            ASSERT_TRUE(db.Scan().has_value());
            ASSERT_TRUE(db.CookAll().failed.empty());
        }
        PoisonProductPath(Root(), hostile);
        WriteFile(Root(), "a.png", SolidPng(200)); // a new source cooks again and forgets the old products
        AssetDatabase db = Make();
        ASSERT_TRUE(db.Scan().has_value());
        ASSERT_NE(db.FindByPath("a.png"), nullptr);
        EXPECT_TRUE(db.FindByPath("a.png")->products.empty()) << hostile; // the hostile entry was dropped on load
        const CookReport report = db.CookAll();
        EXPECT_EQ(report.cooked, (std::vector<std::string>{"a.png"})) << hostile;
        EXPECT_TRUE(std::filesystem::exists(outside.Path() / "victim.txt")) << hostile;
        EXPECT_TRUE(std::filesystem::exists(Root() / "keep/inside.txt")) << hostile;
    }
}

TEST_F(AssetDatabaseTest, Prune_MetaProductsOutsideImported_NothingDeleted)
{
    interi::testing::ScopedTempDir outside;
    WriteText(outside.Path(), "victim.txt", "keep");
    WriteText(Root(), "keep/inside.txt", "keep");
    const std::string absolute = (outside.Path() / "victim.txt").generic_string();
    const std::string traversal = "../" + outside.Path().filename().string() + "/victim.txt";
    for (const std::string& hostile :
         {absolute, traversal, std::string("imported/../keep/inside.txt"), std::string("keep/inside.txt")})
    {
        WriteFile(Root(), "a.png", SolidPng(10));
        std::filesystem::remove(Root() / "a.png.meta");
        {
            AssetDatabase db = Make();
            ASSERT_TRUE(db.Scan().has_value());
            ASSERT_TRUE(db.CookAll().failed.empty());
        }
        PoisonProductPath(Root(), hostile);
        std::filesystem::remove(Root() / "a.png");
        AssetDatabase db = Make();
        (void)db.Prune();
        EXPECT_TRUE(std::filesystem::exists(outside.Path() / "victim.txt")) << hostile;
        EXPECT_TRUE(std::filesystem::exists(Root() / "keep/inside.txt")) << hostile;
    }
}

TEST_F(AssetDatabaseTest, Prune_ImportedFolderSymlinkedOutside_TargetSurvives)
{
    // The imported folder itself is a link out of the root, and a stale .meta names a product "inside" it.
    interi::testing::ScopedTempDir outside;
    WriteText(outside.Path(), "a.png.itex", "keep");
    WriteFile(Root(), "a.png", SolidPng(10));
    {
        AssetDatabase db = Make();
        ASSERT_TRUE(db.Scan().has_value());
        ASSERT_TRUE(db.CookAll().failed.empty());
    }
    std::filesystem::remove_all(Root() / "imported");
    std::error_code error;
    std::filesystem::create_directory_symlink(outside.Path(), Root() / "imported", error);
    if (error)
    {
        GTEST_SKIP() << "symlinks are not available: " << error.message();
    }
    std::filesystem::remove(Root() / "a.png");
    AssetDatabase db = Make();
    (void)db.Prune();
    EXPECT_TRUE(std::filesystem::exists(outside.Path() / "a.png.itex"));
    EXPECT_FALSE(std::filesystem::exists(Root() / "a.png.meta"));
}

TEST_F(AssetDatabaseTest, Prune_ProductSymlinkedOutside_TargetSurvives)
{
    interi::testing::ScopedTempDir outside;
    WriteText(outside.Path(), "victim.txt", "keep");
    WriteFile(Root(), "a.png", SolidPng(10));
    {
        AssetDatabase db = Make();
        ASSERT_TRUE(db.Scan().has_value());
        ASSERT_TRUE(db.CookAll().failed.empty());
    }
    // The recorded product becomes a link to a file outside the root, and the directory above it does too.
    std::filesystem::remove(Root() / "imported/a.png.itex");
    std::error_code error;
    std::filesystem::create_symlink(outside.Path() / "victim.txt", Root() / "imported/a.png.itex", error);
    if (error)
    {
        GTEST_SKIP() << "symlinks are not available: " << error.message();
    }
    std::filesystem::remove(Root() / "a.png");
    AssetDatabase db = Make();
    (void)db.Prune();
    EXPECT_TRUE(std::filesystem::exists(outside.Path() / "victim.txt"));
    EXPECT_FALSE(std::filesystem::exists(Root() / "a.png.meta"));
}
