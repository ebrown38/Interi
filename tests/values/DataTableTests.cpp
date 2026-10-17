// SPDX-License-Identifier: MIT
// Data tables of the shared SDK test project (tests/sdk/modules): com.test.a declares com.test.a:Guns (range: float
// 0..100, default 50, renamed from "reach"; power: int 1..10, default 5; mode: enum Single/Auto).
#include <interi/values/DataRegistry.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace interi;
using namespace interi::values;

namespace
{

constexpr std::string_view kGuns = "com.test.a:Guns";

class DataTables : public ::testing::Test
{
protected:
    void SetUp() override
    {
        for (const std::size_t index : graph.loadOrder)
        {
            Result<ModuleContent> content = LoadModuleContent(graph.modules[index].manifest);
            ASSERT_TRUE(content.has_value()) << content.error().message;
            ASSERT_TRUE(data.AddModule(*content).has_value());
        }
    }

    [[nodiscard]] DataTableRegistry& Tables() noexcept { return data.Tables(); }

    sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
    DataRegistry data{modules};
};

} // namespace

TEST_F(DataTables, Schema_FromModule)
{
    const DataTableDefinition* guns = Tables().Table(kGuns);
    ASSERT_NE(guns, nullptr);
    ASSERT_EQ(guns->columns.size(), 3u);
    EXPECT_EQ(guns->columns[0].name, "range");
    EXPECT_EQ(guns->columns[0].renamedFrom, std::vector<std::string>{"reach"});
    EXPECT_EQ(guns->columns[1].schema.type, ValueType::Int);
    EXPECT_EQ(Tables().TableIds(), std::vector<std::string>{std::string(kGuns)});
}

TEST_F(DataTables, Rows_CellsDefaultUntilSet)
{
    ASSERT_TRUE(Tables().AddRow(kGuns, "shotgun").has_value());
    EXPECT_EQ(Tables().Get<float>(kGuns, "shotgun", "range"), 50.0f);
    ASSERT_TRUE(Tables().SetCell(kGuns, "shotgun", "range", 20.0f).has_value());
    ASSERT_TRUE(Tables().SetCell(kGuns, "shotgun", "mode", EnumValue{.name = "Auto"}).has_value());
    EXPECT_EQ(Tables().Get<float>(kGuns, "shotgun", "range"), 20.0f);
    EXPECT_EQ(Tables().Get<EnumValue>(kGuns, "shotgun", "mode").value_or(EnumValue{}).name, "Auto");
    EXPECT_EQ(Tables().Get<float>(kGuns, "shotgun", "power").error().code, ErrorCode::InvalidArgument);

    ASSERT_TRUE(Tables().AddRow(kGuns, "pistol").has_value());
    EXPECT_EQ(Tables().RowIds(kGuns), (std::vector<std::string>{"pistol", "shotgun"}));
    EXPECT_FALSE(Tables().AddRow(kGuns, "pistol").has_value()) << "duplicate";
    EXPECT_FALSE(Tables().AddRow(kGuns, "bad id").has_value());
    EXPECT_EQ(Tables().AddRow("com.test.a:Nothing", "x").error().code, ErrorCode::NotFound);

    EXPECT_TRUE(Tables().ClearCell(kGuns, "shotgun", "range"));
    EXPECT_EQ(Tables().Get<float>(kGuns, "shotgun", "range"), 50.0f);
    EXPECT_TRUE(Tables().RemoveRow(kGuns, "pistol"));
    EXPECT_FALSE(Tables().HasRow(kGuns, "pistol"));
    EXPECT_FALSE(Tables().Cell(kGuns, "pistol", "range").has_value());
}

TEST_F(DataTables, DataTable_RowViolatesRange_Error)
{
    ASSERT_TRUE(Tables().AddRow(kGuns, "shotgun").has_value());
    const Result<void> set = Tables().SetCell(kGuns, "shotgun", "range", 120.0f);
    ASSERT_FALSE(set.has_value());
    EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(set.error().message.find("row 'shotgun'"), std::string::npos) << set.error().message;
    EXPECT_NE(set.error().message.find("column 'range'"), std::string::npos) << set.error().message;
    EXPECT_NE(set.error().message.find("maximum 100"), std::string::npos) << set.error().message;

    // Rows loaded from a project aren't checked on the way in; Validate locates every problem.
    Tables().LoadRow(
        kGuns, "rifle",
        DataRow{{"range", Value(120.0f)}, {"power", Value(std::int32_t{3})}, {"colour", Value(std::string("red"))}});
    const std::vector<TableIssue> issues = Tables().Validate();
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].row, "rifle");
    EXPECT_EQ(issues[0].column, "colour");
    EXPECT_EQ(issues[1].column, "range");
    EXPECT_EQ(issues[1].Text(), "table 'com.test.a:Guns', row 'rifle', column 'range': 120 is above the maximum 100");
    EXPECT_EQ(Tables().Get<float>(kGuns, "rifle", "range"), 50.0f) << "an invalid cell reads as the default";
    EXPECT_EQ(Tables().Get<std::int32_t>(kGuns, "rifle", "power"), 3);
}

TEST_F(DataTables, ModuleGone_RowsKeptAndReported)
{
    ASSERT_TRUE(Tables().AddRow(kGuns, "shotgun").has_value());
    ExtractedData a = data.ExtractProvider("com.test.a");
    EXPECT_EQ(Tables().Table(kGuns), nullptr);
    EXPECT_TRUE(Tables().HasRow(kGuns, "shotgun"));
    const std::vector<std::string> warnings = data.Revalidate();
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("no module declares this table"), std::string::npos);
    data.Restore(std::move(a));
    EXPECT_TRUE(data.Revalidate().empty());
}

TEST(DataTableSchema, Errors)
{
    const auto parse = [](const std::string& tables)
    { return ParseModuleContent(R"({"id": "com.x.y", "dataTables": )" + tables + "}", "com.x.y"); };
    EXPECT_FALSE(parse(R"([{"id": "T"}])").has_value()) << "no columns";
    EXPECT_FALSE(parse(R"([{"id": "T", "columns": [{"name": "a"}]}])").has_value()) << "no type";
    EXPECT_FALSE(
        parse(R"([{"id": "T", "columns": [{"name": "a", "type": "int"}, {"name": "a", "type": "int"}]}])").has_value())
        << "duplicate column";
    EXPECT_FALSE(
        parse(
            R"([{"id": "T", "columns": [{"name": "a", "type": "int"}, {"name": "b", "type": "int", "renamedFrom": ["a"]}]}])")
            .has_value())
        << "renamed from a live column";
    EXPECT_FALSE(parse(R"([{"id": "T", "columns": [{"name": "a b", "type": "int"}]}])").has_value());
    const Result<ModuleContent> content =
        parse(R"([{"id": "T", "version": 2, "columns": [{"name": "a", "type": "int"}]}])");
    ASSERT_TRUE(content.has_value()) << content.error().message;
    ASSERT_EQ(content->dataTables.size(), 1u);
    EXPECT_EQ(content->dataTables[0].id, "com.x.y:T");
    EXPECT_EQ(content->dataTables[0].version, 2u);
}
