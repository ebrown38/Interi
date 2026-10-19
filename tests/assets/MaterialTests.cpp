// SPDX-License-Identifier: MIT
#include <interi/assets/Material.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::assets;

namespace
{

PbrMaterial MakeMaterial()
{
    PbrMaterial m;
    m.name = "bronze";
    m.baseColor = Vec4(0.8f, 0.5f, 0.2f, 0.75f);
    m.metallic = 0.9f;
    m.roughness = 0.35f;
    m.emissive = Vec3(0.1f, 0.0f, 0.0f);
    m.normalScale = 0.5f;
    m.occlusionStrength = 0.8f;
    m.alphaMode = AlphaMode::Mask;
    m.alphaCutoff = 0.3f;
    m.doubleSided = true;
    m.baseColorTexture = {.path = "textures/bronze.itex", .guid = Guid{1, 2}, .uvSet = 1};
    m.normalTexture = {.path = "textures/bronze_n.itex", .guid = Guid{}, .uvSet = 0};
    return m;
}

} // namespace

TEST(Material, WriteThenParse_RoundTrips)
{
    const PbrMaterial material = MakeMaterial();
    auto parsed = ParseMaterial(WriteMaterial(material));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->material, material);
    EXPECT_TRUE(parsed->warnings.empty());
}

TEST(Material, EmptyObject_GivesGltfDefaults)
{
    auto parsed = ParseMaterial("{}");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->material, PbrMaterial{});
    EXPECT_FLOAT_EQ(parsed->material.metallic, 1.0f);
    EXPECT_EQ(parsed->material.alphaMode, AlphaMode::Opaque);
}

TEST(Material, UnknownFields_AreWarnings)
{
    auto parsed = ParseMaterial(R"json({"roughness": 0.5, "sheen": 1, "textures": {"clearcoat": {"path": "a"}}})json");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(parsed->warnings.size(), 2u);
    EXPECT_FLOAT_EQ(parsed->material.roughness, 0.5f);
}

TEST(Material, Malformed_IsAnError)
{
    EXPECT_FALSE(ParseMaterial("[]").has_value());
    EXPECT_FALSE(ParseMaterial("{").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"metallic": "high"})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"metallic": 2})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"baseColor": [1, 1, 1]})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"alphaMode": "glass"})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"textures": {"normal": {}}})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"textures": {"normal": {"path": "a", "guid": "xyz"}}})json").has_value());
    EXPECT_FALSE(ParseMaterial(R"json({"version": 2})json").has_value());
}

TEST(MaterialAsset, WriteThenRead_RoundTrips)
{
    const PbrMaterial material = MakeMaterial();
    auto read = ReadMaterialAsset(WriteMaterialAsset(material));
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, material);
}

TEST(MaterialAsset, FlippedByte_IsCorrupt)
{
    std::vector<std::byte> bytes = WriteMaterialAsset(MakeMaterial());
    bytes[bytes.size() - 5] ^= std::byte{0x01};
    auto read = ReadMaterialAsset(bytes);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, ErrorCode::Corrupt);
}

TEST(MaterialAsset, InvalidFactors_RejectedOnRead)
{
    PbrMaterial material = MakeMaterial();
    material.roughness = 3.0f;
    EXPECT_FALSE(ReadMaterialAsset(WriteMaterialAsset(material)).has_value());
}
