// SPDX-License-Identifier: MIT
#pragma once

// Component reflection: named, typed fields at fixed offsets, keyed by component registry name. The editor's
// inspector and undo stack read and write components through it; Values (ValueSchema) share its types.

#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/values/Value.h>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::values
{

struct FieldInfo
{
    std::string name;
    ValueType type = ValueType::Float;
    std::size_t offset = 0; ///< Bytes from the start of the component.
    ValueMeta meta;
    std::vector<std::string> options; ///< Enum option names (Enum fields; see EnumField).
};

/// The reflected fields of one component type, plus what the editor needs to add it to an entity.
struct TypeReflection
{
    std::string name;         ///< The component's registry name (kComponentName or the compiler's type name).
    ComponentTypeId type = 0; ///< Its id in this process.
    std::vector<FieldInfo> fields;
    void (*addDefault)(World& world, Entity entity) = nullptr; ///< Adds a value-initialised component.

    [[nodiscard]] const FieldInfo* Find(std::string_view field) const noexcept;
};

/// Reads a field of a live component object.
[[nodiscard]] Value ReadField(const FieldInfo& field, const void* component);

/// Writes a field of a live component object. False (nothing written) if the value's type is not the field's.
bool WriteField(const FieldInfo& field, void* component, const Value& value);

/// A field descriptor for `T::*member` (M is a value type's C++ type: bool, std::uint32_t, float, Vec3, Quat,
/// std::string, std::int32_t, Color, EnumValue, AssetRef, EntityRef or ValueList). The offset is measured on a
/// value-initialised T, so T needs no standard layout (offsetof would), only a default constructor.
template <typename T, typename M>
[[nodiscard]] FieldInfo Field(std::string_view name, M T::* member, const ValueMeta& meta = {})
{
    const T probe{};
    const auto* base = reinterpret_cast<const std::byte*>(&probe);
    const auto* at = reinterpret_cast<const std::byte*>(&(probe.*member));
    return FieldInfo{.name = std::string(name),
                     .type = ValueTypeOf<M>(),
                     .offset = static_cast<std::size_t>(at - base),
                     .meta = meta,
                     .options = {}};
}

/// An Enum field with its option names, in display order.
template <typename T>
[[nodiscard]] FieldInfo EnumField(std::string_view name, EnumValue T::* member, std::vector<std::string> options,
                                  const ValueMeta& meta = {})
{
    FieldInfo field = Field(name, member, meta);
    field.options = std::move(options);
    return field;
}

/// Component reflection by registry name. Not thread-safe (one owner thread).
class ReflectionRegistry
{
public:
    /// Registers T's fields (replacing an earlier registration of the same name).
    template <Component T>
    const TypeReflection& Register(std::vector<FieldInfo> fields)
    {
        TypeReflection reflection;
        reflection.type = ComponentId<T>();
        reflection.name = ComponentInfoOf(reflection.type).name;
        reflection.fields = std::move(fields);
        reflection.addDefault = [](World& world, Entity entity) { (void)world.Add<T>(entity, T{}); };
        return Insert(std::move(reflection));
    }

    [[nodiscard]] const TypeReflection* Find(std::string_view name) const noexcept;
    [[nodiscard]] const TypeReflection* Find(ComponentTypeId type) const noexcept;

    /// Every registered type, ordered by name.
    [[nodiscard]] std::vector<const TypeReflection*> Types() const;

private:
    const TypeReflection& Insert(TypeReflection reflection);

    std::map<std::string, TypeReflection, std::less<>> m_types;
};

} // namespace interi::values
