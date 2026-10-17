// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Quat.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace interi::editor
{

/// The field types the inspector can show and the undo stack can record.
enum class FieldType : std::uint8_t
{
    Bool,
    U32,
    Float,
    Vec3,
    Quat,
    String,
};

/// One field's value; the alternative index matches FieldType.
using FieldValue = std::variant<bool, std::uint32_t, float, Vec3, Quat, std::string>;

[[nodiscard]] constexpr FieldType TypeOf(const FieldValue& value) noexcept
{
    return static_cast<FieldType>(value.index());
}

struct FieldInfo
{
    std::string name;
    FieldType type = FieldType::Float;
    std::size_t offset = 0; ///< Bytes from the start of the component.
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
[[nodiscard]] FieldValue ReadField(const FieldInfo& field, const void* component);

/// Writes a field of a live component object. False (nothing written) if the value's type is not the field's.
bool WriteField(const FieldInfo& field, void* component, const FieldValue& value);

namespace detail
{

template <typename M>
[[nodiscard]] consteval FieldType FieldTypeFor() noexcept
{
    if constexpr (std::is_same_v<M, bool>)
    {
        return FieldType::Bool;
    }
    else if constexpr (std::is_same_v<M, std::uint32_t>)
    {
        return FieldType::U32;
    }
    else if constexpr (std::is_same_v<M, float>)
    {
        return FieldType::Float;
    }
    else if constexpr (std::is_same_v<M, Vec3>)
    {
        return FieldType::Vec3;
    }
    else if constexpr (std::is_same_v<M, Quat>)
    {
        return FieldType::Quat;
    }
    else
    {
        static_assert(std::is_same_v<M, std::string>, "unsupported reflected field type");
        return FieldType::String;
    }
}

} // namespace detail

/// A field descriptor for `T::*member`. The offset is measured on a value-initialised T, so T needs no standard
/// layout (offsetof would), only a default constructor.
template <typename T, typename M>
[[nodiscard]] FieldInfo Field(std::string_view name, M T::* member)
{
    const T probe{};
    const auto* base = reinterpret_cast<const std::byte*>(&probe);
    const auto* at = reinterpret_cast<const std::byte*>(&(probe.*member));
    return FieldInfo{
        .name = std::string(name), .type = detail::FieldTypeFor<M>(), .offset = static_cast<std::size_t>(at - base)};
}

/// Component reflection by registry name. The editor owns one; RegisterEngineReflection fills in the engine's
/// components. Not thread-safe (editor thread only).
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

/// Registers Transform, the editor's document kind components (EditorScene.h; EditorIdComponent is the scene's own) and
/// the gameplay components with kComponentNames (VehicleComponent, WreckComponent).
void RegisterEngineReflection(ReflectionRegistry& registry);

} // namespace interi::editor
