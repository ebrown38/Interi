// SPDX-License-Identifier: MIT
#include <interi/values/ModuleContent.h>

#include <gtest/gtest.h>

#include <string>

using namespace interi;
using namespace interi::values;

namespace
{

Result<ModuleContent> Parse(const std::string& values)
{
    return ParseModuleContent(R"({"id": "com.author.fps", "version": "1.0.0", "values": )" + values + "}",
                              "com.author.fps");
}

void ExpectError(const std::string& values, std::string_view fragment)
{
    const Result<ModuleContent> content = Parse(values);
    ASSERT_FALSE(content.has_value()) << values;
    EXPECT_EQ(content.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(content.error().message.find(fragment), std::string::npos) << content.error().message;
}

} // namespace

TEST(ModuleContent, Values_Parsed)
{
    const Result<ModuleContent> content = Parse(R"([
        {"id": "JumpHeight", "type": "float", "default": 1.5, "min": 0, "max": 10, "step": 0.1, "unit": "m",
         "category": "Movement", "tooltip": "How high", "readOnly": true, "version": 3},
        {"id": "value.Gravity", "shared": true, "type": "float", "default": -9.81},
        {"overrides": "com.author.base:Speed", "type": "int", "default": 7},
        {"id": "FireMode", "type": "enum", "options": ["Single", "Auto"]},
        {"id": "Waypoints", "type": "list", "element": "vec3", "default": [[1, 2, 3]]}
    ])");
    ASSERT_TRUE(content.has_value()) << content.error().message;
    EXPECT_TRUE(content->warnings.empty());
    ASSERT_EQ(content->values.size(), 5u);

    const ValueDefinition& jump = content->values[0];
    EXPECT_EQ(jump.id, "com.author.fps:JumpHeight");
    EXPECT_FALSE(jump.isOverride);
    EXPECT_EQ(jump.version, 3u);
    EXPECT_EQ(jump.schema.type, ValueType::Float);
    EXPECT_EQ(jump.schema.defaultValue, Value(1.5f));
    EXPECT_EQ(jump.schema.meta.max, 10.0);
    EXPECT_EQ(jump.schema.meta.unit, "m");
    EXPECT_EQ(jump.schema.meta.category, "Movement");
    EXPECT_TRUE(jump.schema.meta.readOnly);

    EXPECT_EQ(content->values[1].id, "value.Gravity");
    EXPECT_TRUE(content->values[1].schema.meta.shared);
    EXPECT_EQ(content->values[2].id, "com.author.base:Speed");
    EXPECT_TRUE(content->values[2].isOverride);
    EXPECT_EQ(content->values[3].schema.defaultValue, Value(EnumValue{.name = "Single"}));
    EXPECT_EQ(std::get<ValueList>(content->values[4].schema.defaultValue).items.size(), 1u);
}

TEST(ModuleContent, NoSections_Empty)
{
    const Result<ModuleContent> content = ParseModuleContent(R"({"id": "com.a.b"})", "com.a.b");
    ASSERT_TRUE(content.has_value());
    EXPECT_TRUE(content->values.empty());
}

TEST(ModuleContent, Malformed_Errors)
{
    ExpectError(R"({})", "must be an array");
    ExpectError(R"([{"id": "X"}])", "missing \"type\"");
    ExpectError(R"([{"id": "X", "type": "double"}])", "unknown type");
    ExpectError(R"([{"type": "float"}])", "exactly one of");
    ExpectError(R"([{"id": "X", "overrides": "com.a.b:X", "type": "float"}])", "exactly one of");
    ExpectError(R"([{"id": "com.x:Y", "type": "float"}])", "not an item name");
    ExpectError(R"([{"overrides": "Speed", "type": "float"}])", "not a local id");
    ExpectError(R"([{"id": "X", "type": "float", "default": 11, "max": 10}])", "maximum");
    ExpectError(R"([{"id": "X", "type": "float", "default": "high"}])", "expected a number");
    ExpectError(R"([{"id": "X", "type": "float"}, {"id": "X", "type": "int"}])", "declared twice");
    ExpectError(R"([{"id": "X", "type": "list"}])", "missing \"element\"");
    ExpectError(R"([{"id": "X", "type": "enum"}])", "needs options");
    ExpectError(R"([{"id": "X", "type": "float", "version": -1}])", "non-negative");
}

TEST(ModuleContent, UnknownKey_Warning)
{
    const Result<ModuleContent> content = Parse(R"([{"id": "X", "type": "float", "colour": "red"}])");
    ASSERT_TRUE(content.has_value());
    ASSERT_EQ(content->warnings.size(), 1u);
    EXPECT_NE(content->warnings[0].find("colour"), std::string::npos);
}
