// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/destruction/Material.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace
{

using interi::ErrorCode;
using interi::destruction::DestructionMaterial;
using interi::destruction::FindMaterial;
using interi::destruction::LoadMaterials;
using interi::destruction::ParseMaterials;

bool AnyWarningContains(const std::vector<std::string>& warnings, std::string_view text)
{
    return std::ranges::any_of(warnings, [&](const std::string& w) { return w.find(text) != std::string::npos; });
}

} // namespace

TEST(MaterialLoader, MissingField_UsesDefaultAndWarns)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "brick", "density": 1900,
        "compressiveStrength": 2.0e7, "tensileStrength": 1.5e6, "fractureEnergy": 60, "debrisMinSize": 0.08,
        "interiorMaterialId": 7 } ] })");
    ASSERT_TRUE(set.has_value()) << set.error().message;
    ASSERT_EQ(set->materials.size(), 1u);
    const DestructionMaterial& brick = set->materials[0];
    EXPECT_EQ(brick.name, "brick");
    EXPECT_EQ(brick.density, 1900.0f);
    EXPECT_EQ(brick.shearStrength, DestructionMaterial{}.shearStrength);
    EXPECT_EQ(brick.interiorMaterialId, 7u);
    ASSERT_EQ(set->warnings.size(), 1u);
    EXPECT_TRUE(AnyWarningContains(set->warnings, "shearStrength"));
}

TEST(MaterialLoader, NegativeStrength_Rejected)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "bad", "tensileStrength": -5 } ] })");
    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(set.error().message.find("tensileStrength"), std::string::npos);
}

TEST(MaterialLoader, ZeroDensity_Rejected)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "bad", "density": 0 } ] })");
    ASSERT_FALSE(set.has_value());
    EXPECT_NE(set.error().message.find("density"), std::string::npos);
}

TEST(MaterialLoader, WrongFieldType_Rejected)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "bad", "density": "heavy" } ] })");
    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument);
}

TEST(MaterialLoader, MalformedJson_Rejected)
{
    EXPECT_FALSE(ParseMaterials(R"({ "materials": [ { "name": )").has_value());
    EXPECT_FALSE(ParseMaterials(R"({ "stuff": [] })").has_value());
    EXPECT_FALSE(ParseMaterials(R"({ "materials": [ { "density": 5 } ] })").has_value());
}

TEST(MaterialLoader, DuplicateName_Rejected)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "a" }, { "name": "a" } ] })");
    ASSERT_FALSE(set.has_value());
    EXPECT_NE(set.error().message.find("duplicate"), std::string::npos);
}

TEST(MaterialLoader, UnknownField_Warns)
{
    const auto set = ParseMaterials(R"({ "materials": [ { "name": "a", "density": 1, "compressiveStrength": 1,
        "tensileStrength": 1, "shearStrength": 1, "fractureEnergy": 1, "debrisMinSize": 1, "interiorMaterialId": 0,
        "colour": "red" } ] })");
    ASSERT_TRUE(set.has_value()) << set.error().message;
    ASSERT_EQ(set->warnings.size(), 1u);
    EXPECT_TRUE(AnyWarningContains(set->warnings, "colour"));
}

TEST(MaterialLoader, PresetsFile_LoadsAllPresetsWithoutWarnings)
{
    const auto set = LoadMaterials(INTERI_TEST_MATERIALS_FILE);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    for (const char* name : {"concrete", "steel", "wood", "glass", "metal_sheet"})
    {
        EXPECT_TRUE(FindMaterial(set->materials, name).has_value()) << name;
    }
    const auto& steel = set->materials[*FindMaterial(set->materials, "steel")];
    const auto& concrete = set->materials[*FindMaterial(set->materials, "concrete")];
    EXPECT_GT(steel.tensileStrength, concrete.tensileStrength);
}

TEST(MaterialLoader, MissingFile_IoError)
{
    const auto set = LoadMaterials("does/not/exist/materials.json");
    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::IoError);
}

TEST(Material, Serialize_RoundTrips)
{
    DestructionMaterial material{.name = "glass", .density = 2500.0f, .tensileStrength = 3.3e7f};
    std::vector<std::byte> bytes;
    interi::ArchiveWriter writer(bytes, interi::MakeFourCC("MATL"), 1);
    writer(material);

    auto reader = interi::ArchiveReader::Open(bytes, interi::MakeFourCC("MATL"), 1);
    ASSERT_TRUE(reader.has_value());
    DestructionMaterial loaded;
    (*reader)(loaded);
    ASSERT_TRUE(reader->Finish().has_value());
    EXPECT_EQ(loaded.name, material.name);
    EXPECT_EQ(loaded.density, material.density);
    EXPECT_EQ(loaded.tensileStrength, material.tensileStrength);
    EXPECT_EQ(loaded.debrisMinSize, material.debrisMinSize);
}
