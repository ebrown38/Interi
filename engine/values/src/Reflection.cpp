// SPDX-License-Identifier: MIT
#include <interi/values/Reflection.h>

#include <algorithm>

namespace interi::values
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

Value ReadField(const FieldInfo& field, const void* component)
{
    return VisitValueType(field.type,
                          [&]<typename T>(std::type_identity<T>) { return Value(At<T>(component, field.offset)); });
}

bool WriteField(const FieldInfo& field, void* component, const Value& value)
{
    if (TypeOf(value) != field.type)
    {
        return false;
    }
    std::visit([&]<typename T>(const T& typed) { At<T>(component, field.offset) = typed; }, value.Variant());
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

} // namespace interi::values
