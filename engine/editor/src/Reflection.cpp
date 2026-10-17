// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/gameplay/Vehicle.h>

#include <algorithm>
#include <cstring>

namespace interi::editor
{

namespace
{

template <typename T>
[[nodiscard]] const T& At(const void* component, std::size_t offset) noexcept
{
    return *reinterpret_cast<const T*>(static_cast<const std::byte*>(component) + offset);
}

template <typename T>
[[nodiscard]] T& At(void* component, std::size_t offset) noexcept
{
    return *reinterpret_cast<T*>(static_cast<std::byte*>(component) + offset);
}

} // namespace

const FieldInfo* TypeReflection::Find(std::string_view field) const noexcept
{
    const auto found = std::ranges::find(fields, field, &FieldInfo::name);
    return found == fields.end() ? nullptr : &*found;
}

FieldValue ReadField(const FieldInfo& field, const void* component)
{
    switch (field.type)
    {
    case FieldType::Bool:
        return At<bool>(component, field.offset);
    case FieldType::U32:
        return At<std::uint32_t>(component, field.offset);
    case FieldType::Float:
        return At<float>(component, field.offset);
    case FieldType::Vec3:
        return At<Vec3>(component, field.offset);
    case FieldType::Quat:
        return At<Quat>(component, field.offset);
    case FieldType::String:
        return At<std::string>(component, field.offset);
    }
    return {};
}

bool WriteField(const FieldInfo& field, void* component, const FieldValue& value)
{
    if (TypeOf(value) != field.type)
    {
        return false;
    }
    std::visit([&]<typename T>(const T& typed) { At<T>(component, field.offset) = typed; }, value);
    return true;
}

const TypeReflection* ReflectionRegistry::Find(std::string_view name) const noexcept
{
    const auto found = m_types.find(name);
    return found == m_types.end() ? nullptr : &found->second;
}

const TypeReflection* ReflectionRegistry::Find(ComponentTypeId type) const noexcept
{
    const auto found = std::ranges::find(m_types, type, [](const auto& entry) { return entry.second.type; });
    return found == m_types.end() ? nullptr : &found->second;
}

std::vector<const TypeReflection*> ReflectionRegistry::Types() const
{
    std::vector<const TypeReflection*> types;
    types.reserve(m_types.size());
    for (const auto& [name, reflection] : m_types)
    {
        types.push_back(&reflection);
    }
    return types;
}

const TypeReflection& ReflectionRegistry::Insert(TypeReflection reflection)
{
    std::string name = reflection.name;
    return m_types.insert_or_assign(std::move(name), std::move(reflection)).first->second;
}

void RegisterEngineReflection(ReflectionRegistry& registry)
{
    registry.Register<Transform>({
        Field("position", &Transform::position),
        Field("rotation", &Transform::rotation),
        Field("scale", &Transform::scale),
    });
    registry.Register<StaticBoxComponent>({Field("halfExtents", &StaticBoxComponent::halfExtents)});
    registry.Register<DestructiblePlacementComponent>({Field("asset", &DestructiblePlacementComponent::asset)});
    registry.Register<SpawnPointComponent>({Field("name", &SpawnPointComponent::name)});
    registry.Register<gameplay::VehicleComponent>({Field("health", &gameplay::VehicleComponent::health)});
    registry.Register<gameplay::WreckComponent>({Field("wheel", &gameplay::WreckComponent::wheel)});
}

} // namespace interi::editor
