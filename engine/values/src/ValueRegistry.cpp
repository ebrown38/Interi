// SPDX-License-Identifier: MIT
#include <interi/values/ValueRegistry.h>

#include <format>
#include <limits>

namespace interi::values
{

ValueRegistry::ValueRegistry(const sdk::ModuleSet& modules)
    : m_definitions(modules)
{
    m_forward = m_definitions.Subscribe(
        [this](std::string_view id)
        {
            Recheck(id);
            m_listeners.Notify(id);
        });
}

Result<void> ValueRegistry::Add(std::string_view provider, const ValueDefinition& definition)
{
    if (!definition.isOverride)
    {
        return m_definitions.Provide(provider, definition.id, definition);
    }
    const std::string_view owner = sdk::OwnerOf(definition.id);
    for (const auto& entry : m_definitions.Stack(definition.id))
    {
        if (entry.provider == owner &&
            (entry.value.schema.type != definition.schema.type ||
             (definition.schema.type == ValueType::List && entry.value.schema.element != definition.schema.element)))
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("'{}' overrides '{}' with type {}, but its owner declares {}", provider,
                                         definition.id, ToString(definition.schema.type),
                                         ToString(entry.value.schema.type)));
        }
    }
    return m_definitions.Override(provider, definition.id, definition);
}

Result<void> ValueRegistry::AddModule(const ModuleContent& content)
{
    for (const ValueDefinition& definition : content.values)
    {
        if (const Result<void> added = Add(content.module, definition); !added)
        {
            (void)ExtractProvider(content.module);
            return added;
        }
    }
    return {};
}

ValueRegistry::Entries ValueRegistry::ExtractProvider(std::string_view provider)
{
    return m_definitions.ExtractProvider(provider);
}

void ValueRegistry::Restore(Entries entries)
{
    m_definitions.Restore(std::move(entries));
}

const ValueDefinition* ValueRegistry::Definition(std::string_view id) const noexcept
{
    return m_definitions.Resolve(id);
}

const Value* ValueRegistry::Get(std::string_view id) const noexcept
{
    const ValueDefinition* definition = Definition(id);
    if (definition == nullptr)
    {
        return nullptr;
    }
    const ProjectValue* project = GetProjectValue(id);
    return project != nullptr && project->active ? &project->value : &definition->schema.defaultValue;
}

Result<void> ValueRegistry::SetProjectValue(std::string_view id, Value value)
{
    const ValueDefinition* definition = Definition(id);
    if (definition == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no value '{}'", id));
    }
    if (const Result<void> valid = ValidateValue(definition->schema, value); !valid)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("'{}': {}", id, valid.error().message));
    }
    const auto it = m_project.find(id);
    if (it == m_project.end())
    {
        m_project.emplace(std::string(id), ProjectValue{.value = std::move(value), .active = true});
    }
    else
    {
        it->second = ProjectValue{.value = std::move(value), .active = true};
    }
    m_listeners.Notify(id);
    return {};
}

void ValueRegistry::LoadProjectValue(std::string_view id, Value value)
{
    const auto it = m_project.find(id);
    if (it == m_project.end())
    {
        m_project.emplace(std::string(id), ProjectValue{.value = std::move(value), .active = false});
    }
    else
    {
        it->second.value = std::move(value);
    }
    Recheck(id);
    m_listeners.Notify(id);
}

bool ValueRegistry::ClearProjectValue(std::string_view id)
{
    const auto it = m_project.find(id);
    if (it == m_project.end())
    {
        return false;
    }
    m_project.erase(it);
    m_listeners.Notify(id);
    return true;
}

const ProjectValue* ValueRegistry::GetProjectValue(std::string_view id) const noexcept
{
    const auto it = m_project.find(id);
    return it == m_project.end() ? nullptr : &it->second;
}

std::vector<sdk::ProvenanceEntry> ValueRegistry::Provenance(std::string_view id) const
{
    std::vector<sdk::ProvenanceEntry> result;
    if (const ProjectValue* project = GetProjectValue(id); project != nullptr && project->active)
    {
        result.push_back({.provider = std::string(kProjectProviderId),
                          .rank = std::numeric_limits<std::uint32_t>::max(),
                          .isOverride = true});
    }
    std::vector<sdk::ProvenanceEntry> modules = m_definitions.Provenance(id);
    result.insert(result.end(), modules.begin(), modules.end());
    return result;
}

sdk::Subscription ValueRegistry::Subscribe(std::function<void(std::string_view id)> callback)
{
    const std::uint64_t token = m_listeners.Add(std::move(callback));
    return {[this](std::uint64_t t) { m_listeners.Remove(t); }, token};
}

std::vector<std::string> ValueRegistry::Revalidate()
{
    std::vector<std::string> warnings;
    for (auto& [id, project] : m_project)
    {
        Recheck(id);
        if (project.active)
        {
            continue;
        }
        const ValueDefinition* definition = Definition(id);
        if (definition == nullptr)
        {
            warnings.push_back(std::format("project value '{}': no module defines it; kept but unused", id));
        }
        else
        {
            warnings.push_back(std::format("project value '{}' = {}: {}; using the default {}", id,
                                           FormatValue(project.value),
                                           ValidateValue(definition->schema, project.value).error().message,
                                           FormatValue(definition->schema.defaultValue)));
        }
    }
    return warnings;
}

void ValueRegistry::Recheck(std::string_view id)
{
    const auto it = m_project.find(id);
    if (it == m_project.end())
    {
        return;
    }
    const ValueDefinition* definition = Definition(id);
    it->second.active = definition != nullptr && ValidateValue(definition->schema, it->second.value).has_value();
}

} // namespace interi::values
