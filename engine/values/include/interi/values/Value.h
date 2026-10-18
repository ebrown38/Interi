// SPDX-License-Identifier: MIT
#pragma once

// Typed values: what reflected component fields, module Values, character attributes and data table cells hold.
// One type system serves the editor's inspector, the undo stack, project files and (from M15) Chain Reaction.

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace interi::values
{

/// The value types. The first six are the editor's original field types; later types are appended so indices never
/// change (a Value's index is its type).
enum class ValueType : std::uint8_t
{
    Bool,
    U32,
    Float,
    Vec3,
    Quat,
    String,
    Int,       ///< int32.
    Color,     ///< Linear RGBA.
    Enum,      ///< One of a schema's option names.
    AssetRef,  ///< A virtual path (empty: none).
    EntityRef, ///< A stable entity id (0: none), never an ECS handle.
    List,      ///< A homogeneous list of scalar values (no nested lists).
};

inline constexpr std::size_t kValueTypeCount = 12;

/// Linear RGBA.
struct Color
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    friend constexpr bool operator==(const Color&, const Color&) noexcept = default;
};

/// An enum value: the option's name (diff-friendly, survives reordering the options).
struct EnumValue
{
    std::string name;

    friend bool operator==(const EnumValue&, const EnumValue&) = default;
};

/// A reference to an asset by virtual path ("meshes/crate.gltf"); empty for none.
struct AssetRef
{
    std::string path;

    friend bool operator==(const AssetRef&, const AssetRef&) = default;
};

/// A reference to an entity by its stable id (an editor or scene id that survives save, load and undo); 0 for none.
struct EntityRef
{
    std::uint64_t id = 0;

    friend constexpr bool operator==(const EntityRef&, const EntityRef&) noexcept = default;
};

struct Value;

/// A list of values of one scalar type.
struct ValueList
{
    ValueType element = ValueType::Float;
    std::vector<Value> items;

    friend bool operator==(const ValueList& a, const ValueList& b);
};

using ValueVariant = std::variant<bool, std::uint32_t, float, Vec3, Quat, std::string, std::int32_t, Color, EnumValue,
                                  AssetRef, EntityRef, ValueList>;
static_assert(std::variant_size_v<ValueVariant> == kValueTypeCount, "one alternative per ValueType, in order");

/// One value; the alternative index is its ValueType. Derives from the variant (rather than aliasing it) so a
/// ValueList can hold Values.
struct Value : ValueVariant
{
    using ValueVariant::ValueVariant;
    using ValueVariant::operator=;

    Value()
        : ValueVariant(false)
    {
    }

    [[nodiscard]] const ValueVariant& Variant() const noexcept { return *this; }
    [[nodiscard]] ValueVariant& Variant() noexcept { return *this; }

    friend bool operator==(const Value& a, const Value& b) { return a.Variant() == b.Variant(); }
};

inline bool operator==(const ValueList& a, const ValueList& b)
{
    return a.element == b.element && a.items == b.items;
}

[[nodiscard]] constexpr ValueType TypeOf(const Value& value) noexcept
{
    return static_cast<ValueType>(value.index());
}

/// The C++ type that holds a ValueType.
template <ValueType Type>
using ValueTypeT = std::variant_alternative_t<static_cast<std::size_t>(Type), ValueVariant>;

/// The ValueType held by C++ type T (a ValueVariant alternative).
template <typename T>
[[nodiscard]] consteval ValueType ValueTypeOf() noexcept
{
    constexpr auto kIndex = []<std::size_t... I>(std::index_sequence<I...>)
    {
        std::size_t index = kValueTypeCount;
        ((std::is_same_v<T, std::variant_alternative_t<I, ValueVariant>> ? (index = I, 0) : 0), ...);
        return index;
    }(std::make_index_sequence<kValueTypeCount>{});
    static_assert(kIndex < kValueTypeCount, "not a value type");
    return static_cast<ValueType>(kIndex);
}

/// Calls `function(std::type_identity<T>{})` with the C++ type of `type`.
template <typename Function>
decltype(auto) VisitValueType(ValueType type, Function&& function)
{
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> decltype(auto)
    {
        using Result = decltype(function(std::type_identity<std::variant_alternative_t<0, ValueVariant>>{}));
        using Fn = Result (*)(Function&);
        static constexpr Fn kTable[] = {
            +[](Function& f) -> Result
            { return f(std::type_identity<std::variant_alternative_t<I, ValueVariant>>{}); }...};
        return kTable[static_cast<std::size_t>(type)](function);
    }(std::make_index_sequence<kValueTypeCount>{});
}

/// "bool", "u32", "float", "vec3", "quat", "string", "int", "color", "enum", "asset", "entity", "list".
[[nodiscard]] std::string_view ToString(ValueType type) noexcept;
[[nodiscard]] std::optional<ValueType> ParseValueType(std::string_view name) noexcept;

/// True for types a List may hold (everything but List).
[[nodiscard]] constexpr bool IsScalar(ValueType type) noexcept
{
    return type != ValueType::List;
}

/// True for types a range (min/max) applies to: U32, Int, Float, and the components of Vec3 and Color.
[[nodiscard]] constexpr bool IsRanged(ValueType type) noexcept
{
    return type == ValueType::U32 || type == ValueType::Int || type == ValueType::Float || type == ValueType::Vec3 ||
           type == ValueType::Color;
}

/// The zero value of a type: false, 0, empty, identity quaternion, opaque black; an empty list of `element`.
[[nodiscard]] Value ZeroValue(ValueType type, ValueType element = ValueType::Float);

/// Editing metadata, for reflected fields and Values alike.
struct ValueMeta
{
    std::optional<double> min; ///< Inclusive; ranged types only.
    std::optional<double> max;
    double step = 0.0; ///< Widget drag step; 0 picks one.
    std::string unit;  ///< Display only ("m", "s", "%").
    std::string category;
    std::string tooltip;
    bool readOnly = false; ///< Not editable in the GUI (chains may still set it at run time).
    bool shared = false;   ///< A shared contract id (Values): modules agree on it; the highest provider wins.
};

/// What a Value may hold: its type plus metadata and default.
struct ValueSchema
{
    ValueType type = ValueType::Float;
    ValueType element = ValueType::Float; ///< List element type.
    std::vector<std::string> options;     ///< Enum option names, in display order.
    ValueMeta meta;
    Value defaultValue = 0.0f;
};

/// Checks a schema: a scalar list element, unique non-empty enum options, a range only on ranged types with
/// min <= max, and a default that passes ValidateValue. InvalidArgument with the reason.
[[nodiscard]] Result<void> ValidateSchema(const ValueSchema& schema);

/// Checks a value against a schema: the type (and list element type), finite floats, the range (per component and
/// per list item), an enum option, a normalised virtual path for an asset. InvalidArgument with the reason.
[[nodiscard]] Result<void> ValidateValue(const ValueSchema& schema, const Value& value);

/// The value as short text for messages and the GUI ("1.5", "(1, 2, 3)", "\"name\"", "[3 items]").
[[nodiscard]] std::string FormatValue(const Value& value);

/// Parses one value from JSON text under a schema (see ValueToJsonText). InvalidArgument when the JSON does not
/// encode the schema's type; the range etc. are not checked (ValidateValue).
[[nodiscard]] Result<Value> ParseValueJson(std::string_view json, const ValueSchema& schema);

/// The value as compact JSON: bool and numbers as such, vec3 [x, y, z], quat [x, y, z, w], color [r, g, b, a],
/// string, enum and asset as strings, entity as an integer id, list as an array. Floats round-trip bit-exactly.
[[nodiscard]] std::string ValueToJsonText(const Value& value);

namespace detail
{

template <typename Archive>
void SerializeScalarPayload(Archive& archive, Value& value, ValueType type)
{
    VisitValueType(type,
                   [&]<typename T>(std::type_identity<T>)
                   {
                       if constexpr (std::is_same_v<T, ValueList>)
                       {
                           INTERI_ASSERT(false, "lists are serialized by Serialize(Archive&, Value&)");
                       }
                       else
                       {
                           if constexpr (Archive::kIsReading)
                           {
                               value = T{};
                           }
                           T& typed = std::get<T>(value);
                           if constexpr (std::is_same_v<T, Quat>)
                           {
                               archive(typed.x, typed.y, typed.z, typed.w);
                           }
                           else if constexpr (std::is_same_v<T, Color>)
                           {
                               archive(typed.r, typed.g, typed.b, typed.a);
                           }
                           else if constexpr (std::is_same_v<T, EnumValue>)
                           {
                               archive(typed.name);
                           }
                           else if constexpr (std::is_same_v<T, AssetRef>)
                           {
                               archive(typed.path);
                           }
                           else if constexpr (std::is_same_v<T, EntityRef>)
                           {
                               archive(typed.id);
                           }
                           else
                           {
                               archive(typed);
                           }
                       }
                   });
}

} // namespace detail

/// Archive format: u8 type, then the payload (a list: u8 element type, u32 count, payloads). Floats bit-exact.
/// Reading an unknown type, a nested list or an implausible count fails the archive.
template <typename Archive>
void Serialize(Archive& archive, Value& value)
{
    auto type = TypeOf(value);
    archive(type);
    if constexpr (Archive::kIsReading)
    {
        if (!archive.Ok() || static_cast<std::size_t>(type) >= kValueTypeCount)
        {
            archive.Fail();
            value = Value{};
            return;
        }
    }
    if (type != ValueType::List)
    {
        detail::SerializeScalarPayload(archive, value, type);
        return;
    }
    if constexpr (Archive::kIsReading)
    {
        value = ValueList{};
    }
    auto& list = std::get<ValueList>(value);
    archive(list.element);
    auto count = static_cast<std::uint32_t>(list.items.size());
    archive(count);
    if constexpr (Archive::kIsReading)
    {
        // Every payload takes at least one byte, so a count beyond the data is corrupt.
        if (!archive.Ok() || static_cast<std::size_t>(list.element) >= kValueTypeCount || !IsScalar(list.element) ||
            count > archive.Remaining())
        {
            archive.Fail();
            value = Value{};
            return;
        }
        list.items.resize(count);
    }
    for (Value& item : list.items)
    {
        detail::SerializeScalarPayload(archive, item, list.element);
    }
}

} // namespace interi::values
