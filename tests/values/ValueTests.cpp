// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/values/Value.h>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace interi;
using namespace interi::values;

namespace
{

/// One value of every type.
std::vector<Value> EveryType()
{
    return {
        true,
        std::uint32_t{4000000000u},
        -0.0f,
        Vec3(0.1f, -2.5e-38f, 3.4e38f),
        Quat{0.1f, 0.2f, 0.3f, 0.9f},
        std::string("a \"quoted\" name"),
        std::int32_t{std::numeric_limits<std::int32_t>::min()},
        Color{.r = 1.0f / 3.0f, .g = 0.0f, .b = 1.0f, .a = 0.5f},
        EnumValue{.name = "Burst"},
        AssetRef{.path = "audio/shot.wav"},
        EntityRef{.id = std::numeric_limits<std::uint64_t>::max()},
        ValueList{.element = ValueType::Float, .items = {0.1f, std::numeric_limits<float>::denorm_min(), 7.0f}},
    };
}

ValueSchema SchemaOf(const Value& value)
{
    ValueSchema schema;
    schema.type = TypeOf(value);
    if (const auto* list = std::get_if<ValueList>(&value))
    {
        schema.element = list->element;
    }
    schema.defaultValue = value;
    return schema;
}

std::vector<std::uint32_t> FloatBits(const Value& value)
{
    std::vector<std::uint32_t> bits;
    if (const auto* f = std::get_if<float>(&value))
    {
        bits.push_back(std::bit_cast<std::uint32_t>(*f));
    }
    else if (const auto* v = std::get_if<Vec3>(&value))
    {
        bits = {std::bit_cast<std::uint32_t>(v->x), std::bit_cast<std::uint32_t>(v->y),
                std::bit_cast<std::uint32_t>(v->z)};
    }
    else if (const auto* list = std::get_if<ValueList>(&value))
    {
        for (const Value& item : list->items)
        {
            const auto itemBits = FloatBits(item);
            bits.insert(bits.end(), itemBits.begin(), itemBits.end());
        }
    }
    return bits;
}

} // namespace

TEST(Value, TypeNames_RoundTrip)
{
    for (std::size_t i = 0; i < kValueTypeCount; ++i)
    {
        const auto type = static_cast<ValueType>(i);
        EXPECT_EQ(ParseValueType(ToString(type)), type) << ToString(type);
    }
    EXPECT_FALSE(ParseValueType("double").has_value());
}

TEST(Value, IndexIsType)
{
    const std::vector<Value> values = EveryType();
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        EXPECT_EQ(TypeOf(values[i]), static_cast<ValueType>(i));
        EXPECT_EQ(TypeOf(ZeroValue(static_cast<ValueType>(i))), static_cast<ValueType>(i));
    }
    EXPECT_EQ(std::get<Quat>(ZeroValue(ValueType::Quat)), Quat::Identity());
    EXPECT_EQ(std::get<ValueList>(ZeroValue(ValueType::List, ValueType::Int)).element, ValueType::Int);
}

TEST(Value, Equality_ComparesTypeAndContent)
{
    EXPECT_EQ(Value(1.0f), Value(1.0f));
    EXPECT_NE(Value(1.0f), Value(std::int32_t{1}));
    const Value a = ValueList{.element = ValueType::Int, .items = {std::int32_t{1}}};
    Value b = a;
    EXPECT_EQ(a, b);
    std::get<ValueList>(b).items.emplace_back(std::int32_t{2});
    EXPECT_NE(a, b);
}

TEST(Value, Json_RoundTripsBitExact)
{
    for (const Value& value : EveryType())
    {
        const std::string json = ValueToJsonText(value);
        const Result<Value> parsed = ParseValueJson(json, SchemaOf(value));
        ASSERT_TRUE(parsed.has_value()) << json << ": " << parsed.error().message;
        EXPECT_EQ(*parsed, value) << json;
        EXPECT_EQ(FloatBits(*parsed), FloatBits(value)) << json;
    }
}

TEST(Value, Json_WrongShape_Error)
{
    ValueSchema schema;
    schema.type = ValueType::Vec3;
    EXPECT_FALSE(ParseValueJson("[1, 2]", schema).has_value());
    EXPECT_FALSE(ParseValueJson("\"x\"", schema).has_value());
    schema.type = ValueType::Int;
    EXPECT_FALSE(ParseValueJson("1.5", schema).has_value());
    EXPECT_FALSE(ParseValueJson("4294967296", schema).has_value());
    EXPECT_TRUE(ParseValueJson("-7", schema).has_value());
    schema.type = ValueType::U32;
    EXPECT_FALSE(ParseValueJson("-1", schema).has_value());
    schema.type = ValueType::List;
    schema.element = ValueType::Bool;
    EXPECT_FALSE(ParseValueJson("[true, 1]", schema).has_value());
    EXPECT_FALSE(ParseValueJson("{", schema).has_value());
    schema.type = ValueType::Color;
    const Result<Value> rgb = ParseValueJson("[1, 0.5, 0]", schema);
    ASSERT_TRUE(rgb.has_value());
    EXPECT_EQ(std::get<Color>(*rgb).a, 1.0f);
}

TEST(Value, Archive_RoundTripsBitExact)
{
    std::vector<Value> values = EveryType();
    std::vector<std::byte> bytes;
    ArchiveWriter writer(bytes, MakeFourCC("TVAL"), 1);
    writer(values);

    std::vector<Value> read;
    Result<ArchiveReader> reader = ArchiveReader::Open(bytes, MakeFourCC("TVAL"), 1);
    ASSERT_TRUE(reader.has_value());
    (*reader)(read);
    ASSERT_TRUE(reader->Finish().has_value());
    ASSERT_EQ(read.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        EXPECT_EQ(read[i], values[i]) << i;
        EXPECT_EQ(FloatBits(read[i]), FloatBits(values[i])) << i;
    }

    std::vector<std::byte> again;
    ArchiveWriter rewriter(again, MakeFourCC("TVAL"), 1);
    rewriter(read);
    EXPECT_EQ(again, bytes);
}

TEST(Value, Archive_UnknownTypeOrNestedList_Fails)
{
    std::vector<std::byte> bytes;
    ArchiveWriter writer(bytes, MakeFourCC("TVAL"), 1);
    auto unknown = static_cast<std::uint8_t>(kValueTypeCount);
    writer(unknown);
    Value value;
    Result<ArchiveReader> reader = ArchiveReader::Open(bytes, MakeFourCC("TVAL"), 1);
    ASSERT_TRUE(reader.has_value());
    (*reader)(value);
    EXPECT_FALSE(reader->Finish().has_value());

    bytes.clear();
    ArchiveWriter nested(bytes, MakeFourCC("TVAL"), 1);
    auto list = static_cast<std::uint8_t>(ValueType::List);
    std::uint32_t count = 1;
    nested(list, list, count);
    reader = ArchiveReader::Open(bytes, MakeFourCC("TVAL"), 1);
    ASSERT_TRUE(reader.has_value());
    (*reader)(value);
    EXPECT_FALSE(reader->Finish().has_value());

    bytes.clear();
    ArchiveWriter huge(bytes, MakeFourCC("TVAL"), 1);
    auto element = static_cast<std::uint8_t>(ValueType::Bool);
    count = 0xFFFFFFFFu;
    huge(list, element, count);
    reader = ArchiveReader::Open(bytes, MakeFourCC("TVAL"), 1);
    ASSERT_TRUE(reader.has_value());
    (*reader)(value);
    EXPECT_FALSE(reader->Finish().has_value());
}

TEST(Value, Validate_Range)
{
    ValueSchema schema;
    schema.type = ValueType::Float;
    schema.meta.min = 0.0;
    schema.meta.max = 10.0;
    schema.defaultValue = 1.0f;
    ASSERT_TRUE(ValidateSchema(schema).has_value());
    EXPECT_TRUE(ValidateValue(schema, 10.0f).has_value());
    const Result<void> above = ValidateValue(schema, 10.5f);
    ASSERT_FALSE(above.has_value());
    EXPECT_NE(above.error().message.find("maximum"), std::string::npos);
    EXPECT_FALSE(ValidateValue(schema, -0.5f).has_value());
    EXPECT_FALSE(ValidateValue(schema, std::numeric_limits<float>::quiet_NaN()).has_value());
    EXPECT_FALSE(ValidateValue(schema, std::int32_t{1}).has_value());

    schema.type = ValueType::Vec3;
    schema.defaultValue = Vec3(1.0f);
    EXPECT_TRUE(ValidateValue(schema, Vec3(0, 5, 10)).has_value());
    EXPECT_FALSE(ValidateValue(schema, Vec3(0, 11, 0)).has_value());

    schema.type = ValueType::List;
    schema.element = ValueType::Int;
    schema.defaultValue = ValueList{.element = ValueType::Int, .items = {}};
    EXPECT_TRUE(ValidateValue(schema, ValueList{.element = ValueType::Int, .items = {std::int32_t{3}}}).has_value());
    EXPECT_FALSE(ValidateValue(schema, ValueList{.element = ValueType::Int, .items = {std::int32_t{11}}}).has_value());
    EXPECT_FALSE(ValidateValue(schema, ValueList{.element = ValueType::Float, .items = {}}).has_value());
}

TEST(Value, Validate_EnumAndAsset)
{
    ValueSchema schema;
    schema.type = ValueType::Enum;
    schema.options = {"Single", "Burst", "Auto"};
    schema.defaultValue = EnumValue{.name = "Single"};
    ASSERT_TRUE(ValidateSchema(schema).has_value());
    EXPECT_TRUE(ValidateValue(schema, EnumValue{.name = "Auto"}).has_value());
    EXPECT_FALSE(ValidateValue(schema, EnumValue{.name = "auto"}).has_value());

    schema = {};
    schema.type = ValueType::AssetRef;
    schema.defaultValue = AssetRef{};
    EXPECT_TRUE(ValidateValue(schema, AssetRef{.path = "meshes/a.gltf"}).has_value());
    EXPECT_FALSE(ValidateValue(schema, AssetRef{.path = "../secret"}).has_value());
    EXPECT_FALSE(ValidateValue(schema, AssetRef{.path = "meshes\\a.gltf"}).has_value());
}

TEST(Value, ValidateSchema_Errors)
{
    ValueSchema schema;
    schema.type = ValueType::String;
    schema.defaultValue = std::string();
    schema.meta.min = 0.0;
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "strings have no range";

    schema = {};
    schema.meta.min = 2.0;
    schema.meta.max = 1.0;
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "min > max";

    schema = {};
    schema.meta.max = 1.0;
    schema.defaultValue = 2.0f;
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "default out of range";

    schema = {};
    schema.type = ValueType::Enum;
    schema.defaultValue = EnumValue{};
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "no options";
    schema.options = {"A", "A"};
    schema.defaultValue = EnumValue{.name = "A"};
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "duplicate option";

    schema = {};
    schema.type = ValueType::List;
    schema.element = ValueType::List;
    schema.defaultValue = ValueList{.element = ValueType::List, .items = {}};
    EXPECT_FALSE(ValidateSchema(schema).has_value()) << "nested list";
}

TEST(Value, Format_ShortText)
{
    EXPECT_EQ(FormatValue(1.5f), "1.5");
    EXPECT_EQ(FormatValue(Vec3(1, 2, 3)), "(1, 2, 3)");
    EXPECT_EQ(FormatValue(std::string("x")), "\"x\"");
    EXPECT_EQ(FormatValue(EntityRef{}), "(none)");
    EXPECT_EQ(FormatValue(ValueList{.element = ValueType::Int, .items = {std::int32_t{1}}}), "[1 item]");
}
