// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>
#include <interi/values/Value.h>

#include "ValueJson.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

namespace interi::values
{

namespace
{

constexpr std::array<std::string_view, kValueTypeCount> kTypeNames = {
    "bool", "u32", "float", "vec3", "quat", "string", "int", "color", "enum", "asset", "entity", "list",
};

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

Result<void> CheckRange(const ValueMeta& meta, double value)
{
    if (meta.min && value < *meta.min)
    {
        return Invalid(std::format("{} is below the minimum {}", value, *meta.min));
    }
    if (meta.max && value > *meta.max)
    {
        return Invalid(std::format("{} is above the maximum {}", value, *meta.max));
    }
    return {};
}

Result<void> CheckFloat(const ValueMeta& meta, float value)
{
    if (!std::isfinite(value))
    {
        return Invalid("floats must be finite");
    }
    return CheckRange(meta, static_cast<double>(value));
}

/// Validates a scalar of the schema's (element) type.
Result<void> ValidateScalar(const ValueSchema& schema, const Value& value)
{
    return std::visit(
        [&]<typename T>(const T& typed) -> Result<void>
        {
            if constexpr (std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::int32_t>)
            {
                return CheckRange(schema.meta, static_cast<double>(typed));
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return CheckFloat(schema.meta, typed);
            }
            else if constexpr (std::is_same_v<T, Vec3>)
            {
                for (const float component : {typed.x, typed.y, typed.z})
                {
                    INTERI_TRY(CheckFloat(schema.meta, component));
                }
                return {};
            }
            else if constexpr (std::is_same_v<T, Color>)
            {
                for (const float component : {typed.r, typed.g, typed.b, typed.a})
                {
                    INTERI_TRY(CheckFloat(schema.meta, component));
                }
                return {};
            }
            else if constexpr (std::is_same_v<T, Quat>)
            {
                for (const float component : {typed.x, typed.y, typed.z, typed.w})
                {
                    INTERI_TRY(CheckFloat({}, component));
                }
                return {};
            }
            else if constexpr (std::is_same_v<T, EnumValue>)
            {
                if (!schema.options.empty() && std::ranges::find(schema.options, typed.name) == schema.options.end())
                {
                    return Invalid(std::format("'{}' is not one of the options", typed.name));
                }
                return {};
            }
            else if constexpr (std::is_same_v<T, AssetRef>)
            {
                if (typed.path.empty())
                {
                    return {};
                }
                const Result<std::string> normalised = NormalizeVirtualPath(typed.path);
                if (!normalised || *normalised != typed.path)
                {
                    return Invalid(std::format("'{}' is not a normalised virtual path", typed.path));
                }
                return {};
            }
            else
            {
                return {};
            }
        },
        value.Variant());
}

std::string FormatFloat(float value)
{
    return std::format("{}", value);
}

} // namespace

std::string_view ToString(ValueType type) noexcept
{
    const auto index = static_cast<std::size_t>(type);
    return index < kTypeNames.size() ? kTypeNames[index] : std::string_view("unknown");
}

std::optional<ValueType> ParseValueType(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kTypeNames, name);
    if (found == kTypeNames.end())
    {
        return std::nullopt;
    }
    return static_cast<ValueType>(found - kTypeNames.begin());
}

Value ZeroValue(ValueType type, ValueType element)
{
    if (type == ValueType::List)
    {
        return ValueList{.element = element, .items = {}};
    }
    return VisitValueType(type, []<typename T>(std::type_identity<T>) { return Value(T{}); });
}

Result<void> ValidateSchema(const ValueSchema& schema)
{
    if (static_cast<std::size_t>(schema.type) >= kValueTypeCount)
    {
        return Invalid("unknown type");
    }
    if (schema.type == ValueType::List && !IsScalar(schema.element))
    {
        return Invalid("lists can't hold lists");
    }
    const ValueType scalar = schema.type == ValueType::List ? schema.element : schema.type;
    if (scalar == ValueType::Enum)
    {
        if (schema.options.empty())
        {
            return Invalid("an enum needs options");
        }
        for (std::size_t i = 0; i < schema.options.size(); ++i)
        {
            if (schema.options[i].empty())
            {
                return Invalid("enum options can't be empty");
            }
            if (std::find(schema.options.begin(), schema.options.begin() + static_cast<std::ptrdiff_t>(i),
                          schema.options[i]) != schema.options.begin() + static_cast<std::ptrdiff_t>(i))
            {
                return Invalid(std::format("enum option '{}' appears twice", schema.options[i]));
            }
        }
    }
    else if (!schema.options.empty())
    {
        return Invalid("only enums have options");
    }
    if ((schema.meta.min || schema.meta.max) && !IsRanged(scalar))
    {
        return Invalid(std::format("a {} has no range", ToString(scalar)));
    }
    if (schema.meta.min && schema.meta.max && *schema.meta.min > *schema.meta.max)
    {
        return Invalid("min is above max");
    }
    if (!std::isfinite(schema.meta.step) || schema.meta.step < 0.0)
    {
        return Invalid("step must be finite and not negative");
    }
    if (const Result<void> valid = ValidateValue(schema, schema.defaultValue); !valid)
    {
        return Invalid(std::format("default: {}", valid.error().message));
    }
    return {};
}

Result<void> ValidateValue(const ValueSchema& schema, const Value& value)
{
    if (TypeOf(value) != schema.type)
    {
        return Invalid(std::format("expected {}, got {}", ToString(schema.type), ToString(TypeOf(value))));
    }
    if (schema.type != ValueType::List)
    {
        return ValidateScalar(schema, value);
    }
    const auto& list = std::get<ValueList>(value);
    if (list.element != schema.element)
    {
        return Invalid(
            std::format("expected a list of {}, got a list of {}", ToString(schema.element), ToString(list.element)));
    }
    ValueSchema element = schema;
    element.type = schema.element;
    for (std::size_t i = 0; i < list.items.size(); ++i)
    {
        if (TypeOf(list.items[i]) != schema.element)
        {
            return Invalid(std::format("item {}: expected {}, got {}", i, ToString(schema.element),
                                       ToString(TypeOf(list.items[i]))));
        }
        if (const Result<void> valid = ValidateScalar(element, list.items[i]); !valid)
        {
            return Invalid(std::format("item {}: {}", i, valid.error().message));
        }
    }
    return {};
}

std::string FormatValue(const Value& value)
{
    return std::visit(
        []<typename T>(const T& typed) -> std::string
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                return typed ? "true" : "false";
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return FormatFloat(typed);
            }
            else if constexpr (std::is_same_v<T, Vec3>)
            {
                return std::format("({}, {}, {})", typed.x, typed.y, typed.z);
            }
            else if constexpr (std::is_same_v<T, Quat>)
            {
                return std::format("({}, {}, {}, {})", typed.x, typed.y, typed.z, typed.w);
            }
            else if constexpr (std::is_same_v<T, Color>)
            {
                return std::format("rgba({}, {}, {}, {})", typed.r, typed.g, typed.b, typed.a);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return std::format("\"{}\"", typed);
            }
            else if constexpr (std::is_same_v<T, EnumValue>)
            {
                return typed.name;
            }
            else if constexpr (std::is_same_v<T, AssetRef>)
            {
                return typed.path.empty() ? std::string("(none)") : typed.path;
            }
            else if constexpr (std::is_same_v<T, EntityRef>)
            {
                return typed.id == 0 ? std::string("(none)") : std::format("#{}", typed.id);
            }
            else if constexpr (std::is_same_v<T, ValueList>)
            {
                return std::format("[{} {}]", typed.items.size(), typed.items.size() == 1 ? "item" : "items");
            }
            else
            {
                return std::format("{}", typed);
            }
        },
        value.Variant());
}

Result<Value> ParseValueJson(std::string_view json, const ValueSchema& schema)
{
    nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded())
    {
        return Invalid("malformed JSON");
    }
    return detail::ValueFromJson(parsed, schema, "value");
}

std::string ValueToJsonText(const Value& value)
{
    return detail::ValueToJson(value).dump();
}

namespace detail
{

namespace
{

Result<float> FloatFrom(const nlohmann::json& json, std::string_view where)
{
    if (!json.is_number())
    {
        return Invalid(std::format("{}: expected a number", where));
    }
    return static_cast<float>(json.get<double>());
}

template <std::size_t N>
Result<std::array<float, N>> FloatsFrom(const nlohmann::json& json, std::size_t minimum, std::string_view where)
{
    if (!json.is_array() || json.size() < minimum || json.size() > N)
    {
        return Invalid(minimum == N ? std::format("{}: expected an array of {} numbers", where, N)
                                    : std::format("{}: expected an array of {} to {} numbers", where, minimum, N));
    }
    std::array<float, N> result{};
    result.fill(1.0f);
    for (std::size_t i = 0; i < json.size(); ++i)
    {
        INTERI_TRY_ASSIGN(component, FloatFrom(json[i], where));
        result[i] = component;
    }
    return result;
}

Result<std::string> StringFrom(const nlohmann::json& json, std::string_view where)
{
    if (!json.is_string())
    {
        return Invalid(std::format("{}: expected a string", where));
    }
    return json.get<std::string>();
}

template <typename Int>
Result<Int> IntegerFrom(const nlohmann::json& json, std::string_view where)
{
    constexpr auto kMin = std::numeric_limits<Int>::min();
    constexpr auto kMax = std::numeric_limits<Int>::max();
    if (json.is_number_unsigned())
    {
        const auto value = json.get<std::uint64_t>();
        if (value <= static_cast<std::uint64_t>(kMax))
        {
            return static_cast<Int>(value);
        }
    }
    else if (json.is_number_integer())
    {
        const auto value = json.get<std::int64_t>();
        const bool fits = value < 0 ? value >= static_cast<std::int64_t>(kMin)
                                    : static_cast<std::uint64_t>(value) <= static_cast<std::uint64_t>(kMax);
        if (fits)
        {
            return static_cast<Int>(value);
        }
    }
    else if (!json.is_number())
    {
        return Invalid(std::format("{}: expected an integer", where));
    }
    else
    {
        return Invalid(std::format("{}: expected an integer, got {}", where, json.dump()));
    }
    return Invalid(std::format("{}: {} is out of range", where, json.dump()));
}

Result<Value> ScalarFromJson(const nlohmann::json& json, ValueType type, std::string_view where)
{
    switch (type)
    {
    case ValueType::Bool:
        if (!json.is_boolean())
        {
            return Invalid(std::format("{}: expected true or false", where));
        }
        return Value(json.get<bool>());
    case ValueType::U32:
    {
        INTERI_TRY_ASSIGN(value, IntegerFrom<std::uint32_t>(json, where));
        return Value(value);
    }
    case ValueType::Int:
    {
        INTERI_TRY_ASSIGN(value, IntegerFrom<std::int32_t>(json, where));
        return Value(value);
    }
    case ValueType::Float:
    {
        INTERI_TRY_ASSIGN(value, FloatFrom(json, where));
        return Value(value);
    }
    case ValueType::Vec3:
    {
        INTERI_TRY_ASSIGN(v, FloatsFrom<3>(json, 3, where));
        return Value(Vec3(v[0], v[1], v[2]));
    }
    case ValueType::Quat:
    {
        INTERI_TRY_ASSIGN(q, FloatsFrom<4>(json, 4, where));
        return Value(Quat{.x = q[0], .y = q[1], .z = q[2], .w = q[3]});
    }
    case ValueType::Color:
    {
        INTERI_TRY_ASSIGN(c, FloatsFrom<4>(json, 3, where));
        return Value(Color{.r = c[0], .g = c[1], .b = c[2], .a = c[3]});
    }
    case ValueType::String:
    {
        INTERI_TRY_ASSIGN(text, StringFrom(json, where));
        return Value(std::move(text));
    }
    case ValueType::Enum:
    {
        INTERI_TRY_ASSIGN(name, StringFrom(json, where));
        return Value(EnumValue{.name = std::move(name)});
    }
    case ValueType::AssetRef:
    {
        INTERI_TRY_ASSIGN(path, StringFrom(json, where));
        return Value(AssetRef{.path = std::move(path)});
    }
    case ValueType::EntityRef:
    {
        INTERI_TRY_ASSIGN(id, IntegerFrom<std::uint64_t>(json, where));
        return Value(EntityRef{.id = id});
    }
    case ValueType::List:
        break;
    }
    return Invalid(std::format("{}: lists can't hold lists", where));
}

} // namespace

Result<Value> ValueFromJson(const nlohmann::json& json, ValueType type, ValueType element, std::string_view where)
{
    if (type != ValueType::List)
    {
        return ScalarFromJson(json, type, where);
    }
    if (!json.is_array())
    {
        return Invalid(std::format("{}: expected an array", where));
    }
    ValueList list{.element = element, .items = {}};
    list.items.reserve(json.size());
    for (std::size_t i = 0; i < json.size(); ++i)
    {
        INTERI_TRY_ASSIGN(item, ScalarFromJson(json[i], element, std::format("{}[{}]", where, i)));
        list.items.push_back(std::move(item));
    }
    return Value(std::move(list));
}

nlohmann::json ValueToJson(const Value& value)
{
    return std::visit(
        []<typename T>(const T& typed) -> nlohmann::json
        {
            if constexpr (std::is_same_v<T, float>)
            {
                return static_cast<double>(typed);
            }
            else if constexpr (std::is_same_v<T, Vec3>)
            {
                return nlohmann::json::array(
                    {static_cast<double>(typed.x), static_cast<double>(typed.y), static_cast<double>(typed.z)});
            }
            else if constexpr (std::is_same_v<T, Quat>)
            {
                return nlohmann::json::array({static_cast<double>(typed.x), static_cast<double>(typed.y),
                                              static_cast<double>(typed.z), static_cast<double>(typed.w)});
            }
            else if constexpr (std::is_same_v<T, Color>)
            {
                return nlohmann::json::array({static_cast<double>(typed.r), static_cast<double>(typed.g),
                                              static_cast<double>(typed.b), static_cast<double>(typed.a)});
            }
            else if constexpr (std::is_same_v<T, EnumValue>)
            {
                return typed.name;
            }
            else if constexpr (std::is_same_v<T, AssetRef>)
            {
                return typed.path;
            }
            else if constexpr (std::is_same_v<T, EntityRef>)
            {
                return typed.id;
            }
            else if constexpr (std::is_same_v<T, ValueList>)
            {
                nlohmann::json array = nlohmann::json::array();
                for (const Value& item : typed.items)
                {
                    array.push_back(ValueToJson(item));
                }
                return array;
            }
            else
            {
                return typed;
            }
        },
        value.Variant());
}

} // namespace detail

} // namespace interi::values
