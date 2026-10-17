// SPDX-License-Identifier: MIT
// Project data JSON and the cooked binary, over the shared SDK test project (tests/sdk/modules; see
// ValueRegistryTests.cpp, CharacterTests.cpp and DataTableTests.cpp for what it declares). com.test.a:Guns is at
// schema version 2 (its "range" column was "reach" in version 1).
#include <interi/values/ProjectData.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <bit>
#include <memory>
#include <string>
#include <vector>

using namespace interi;
using namespace interi::values;

namespace
{

class ProjectDataTest : public ::testing::Test
{
protected:
    [[nodiscard]] std::unique_ptr<DataRegistry> MakeData() const
    {
        auto data = std::make_unique<DataRegistry>(modules);
        for (const std::size_t index : graph.loadOrder)
        {
            Result<ModuleContent> content = LoadModuleContent(graph.modules[index].manifest);
            EXPECT_TRUE(content.has_value()) << content.error().message;
            EXPECT_TRUE(data->AddModule(content.value_or(ModuleContent{})).has_value());
        }
        return data;
    }

    void Fill(DataRegistry& data) const
    {
        ASSERT_TRUE(data.Values().SetProjectValue("com.test.a:JumpHeight", 4.1234567f).has_value());
        ASSERT_TRUE(data.Values().SetProjectValue("value.Gravity", -12.345678f).has_value());
        ASSERT_TRUE(data.Values().SetProjectValue("com.test.a:FireMode", EnumValue{.name = "Auto"}).has_value());
        ASSERT_TRUE(data.Characters().SetProjectDefault("com.test.a:Soldier", "attr.Health", 1.0f / 3.0f).has_value());
        ASSERT_TRUE(data.Tables().AddRow("com.test.a:Guns", "shotgun").has_value());
        ASSERT_TRUE(data.Tables().SetCell("com.test.a:Guns", "shotgun", "range", 0.1f).has_value());
        ASSERT_TRUE(data.Tables().AddRow("com.test.a:Guns", "pistol").has_value());
        ASSERT_TRUE(data.Tables().SetCell("com.test.a:Guns", "pistol", "power", std::int32_t{9}).has_value());
    }

    sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
};

std::uint32_t Bits(const Value* value)
{
    return value == nullptr ? 0u : std::bit_cast<std::uint32_t>(std::get<float>(value->Variant()));
}

} // namespace

TEST_F(ProjectDataTest, Values_RoundTrip_BitExact)
{
    const auto data = MakeData();
    Fill(*data);
    const std::string json = WriteProjectData(*data);

    const auto loaded = MakeData();
    const Result<std::vector<std::string>> warnings = LoadProjectData(json, *loaded);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty()) << warnings->front();
    EXPECT_EQ(WriteProjectData(*loaded), json);
    EXPECT_EQ(Bits(loaded->Values().Get("com.test.a:JumpHeight")), std::bit_cast<std::uint32_t>(4.1234567f));
    EXPECT_EQ(Bits(loaded->Characters().ProjectDefault("com.test.a:Soldier", "attr.Health")),
              std::bit_cast<std::uint32_t>(1.0f / 3.0f));

    // The cooked binary holds the same resolved data, bit-exactly, and is stable.
    const CookedData cooked = CookData(*loaded);
    const std::vector<std::byte> bytes = WriteCookedData(cooked);
    const Result<CookedData> read = ReadCookedData(bytes);
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, cooked);
    EXPECT_EQ(WriteCookedData(*read), bytes);
    EXPECT_EQ(WriteCookedData(CookData(*data)), bytes);

    EXPECT_EQ(Bits(read->FindValue("com.test.a:JumpHeight")), std::bit_cast<std::uint32_t>(4.1234567f));
    EXPECT_EQ(*read->FindValue("value.Gravity"), Value(-12.345678f));
    const CookedTable* guns = read->FindTable("com.test.a:Guns");
    ASSERT_NE(guns, nullptr);
    ASSERT_EQ(guns->rows.size(), 2u);
    EXPECT_EQ(guns->rows[0].id, "pistol");
    EXPECT_EQ(guns->rows[0].cells[1], Value(std::int32_t{9}));
    EXPECT_EQ(guns->rows[1].cells[0], Value(0.1f));
    ASSERT_EQ(read->archetypes.size(), 1u);
    EXPECT_EQ(read->archetypes[0].attributes[0].defaultValue, Value(1.0f / 3.0f));
}

TEST_F(ProjectDataTest, CurrentVersion_InvalidData_ErrorAndNothingApplied)
{
    const auto data = MakeData();
    ASSERT_TRUE(data->Values().SetProjectValue("com.test.a:JumpHeight", 3.0f).has_value());
    const Result<std::vector<std::string>> result = LoadProjectData(R"({
        "format": 1,
        "values": { "value.Gravity": -1, "com.test.a:JumpHeight": 50 },
        "schemas": { "value.Gravity": 1, "com.test.a:JumpHeight": 1 } })",
                                                                    *data);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("com.test.a:JumpHeight"), std::string::npos) << result.error().message;
    EXPECT_EQ(data->Values().Get<float>("com.test.a:JumpHeight"), 3.0f) << "the old project layer is kept";
    EXPECT_EQ(data->Values().Get<float>("value.Gravity"), -5.0f);

    EXPECT_FALSE(LoadProjectData(R"({ "tables": { "com.test.a:Guns": { "shotgun": { "colour": 1 } } },
                                      "schemas": { "com.test.a:Guns": 2 } })",
                                 *data)
                     .has_value())
        << "an unknown column at the current version";
    EXPECT_FALSE(LoadProjectData(R"({ "tables": { "com.test.a:Guns": { "bad row": {} } } })", *data).has_value());
    EXPECT_FALSE(LoadProjectData("[1, 2]", *data).has_value());
    EXPECT_EQ(LoadProjectData(R"({ "format": 2 })", *data).error().code, ErrorCode::VersionMismatch);
}

TEST_F(ProjectDataTest, OlderVersion_Migrates)
{
    const auto data = MakeData();
    const Result<std::vector<std::string>> warnings = LoadProjectData(R"({
        "values": { "com.test.a:JumpHeight": 50, "com.test.a:FireMode": "Burst" },
        "characters": { "com.test.a:Soldier": { "com.test.b:Armor": 7.9, "attr.Mana": 3 } },
        "tables": { "com.test.a:Guns": { "shotgun": { "reach": 30, "power": 2.7, "weight": 4 } } },
        "schemas": { "com.test.a:JumpHeight": 0, "com.test.a:FireMode": 0, "com.test.a:Soldier": 0,
                     "com.test.a:Guns": 1 } })",
                                                                      *data);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_EQ(data->Values().Get<float>("com.test.a:JumpHeight"), 20.0f) << "clamped into B's range";
    EXPECT_EQ(data->Values().Get<EnumValue>("com.test.a:FireMode").value_or(EnumValue{}).name, "Single");
    EXPECT_EQ(*data->Characters().ProjectDefault("com.test.a:Soldier", "com.test.b:Armor"), Value(std::int32_t{7}));
    EXPECT_EQ(data->Characters().ProjectDefault("com.test.a:Soldier", "attr.Mana"), nullptr);
    EXPECT_EQ(data->Tables().Get<float>("com.test.a:Guns", "shotgun", "range"), 30.0f) << "renamed from reach";
    EXPECT_EQ(data->Tables().Get<std::int32_t>("com.test.a:Guns", "shotgun", "power"), 2);
    EXPECT_EQ(data->Tables().StoredRow("com.test.a:Guns", "shotgun")->count("weight"), 0u);
    EXPECT_GE(warnings->size(), 6u);

    // Written back, everything is at the current versions and loads without warnings.
    const auto again = MakeData();
    const Result<std::vector<std::string>> clean = LoadProjectData(WriteProjectData(*data), *again);
    ASSERT_TRUE(clean.has_value()) << clean.error().message;
    EXPECT_TRUE(clean->empty()) << clean->front();
}

TEST_F(ProjectDataTest, UnknownIds_KeptAndWrittenBack)
{
    const auto data = MakeData();
    const Result<std::vector<std::string>> warnings = LoadProjectData(R"({
        "values": { "com.gone.module:Speed": [1, 2, 3] },
        "tables": { "com.gone.module:Cars": { "van": { "seats": 3 } } },
        "schemas": { "com.gone.module:Speed": 4 } })",
                                                                      *data);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_EQ(warnings->size(), 2u);
    const std::string written = WriteProjectData(*data);
    EXPECT_NE(written.find("com.gone.module:Speed"), std::string::npos);
    EXPECT_NE(written.find("\"seats\": 3"), std::string::npos);
    EXPECT_NE(written.find("\"com.gone.module:Speed\": 4"), std::string::npos) << written;
}

TEST_F(ProjectDataTest, Cooked_Corrupt_Rejected)
{
    const auto data = MakeData();
    Fill(*data);
    std::vector<std::byte> bytes = WriteCookedData(CookData(*data));
    std::vector<std::byte> flipped = bytes;
    flipped[flipped.size() - 3] ^= std::byte{0x40};
    EXPECT_EQ(ReadCookedData(flipped).error().code, ErrorCode::Corrupt);
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(ReadCookedData(bytes).has_value());
}
