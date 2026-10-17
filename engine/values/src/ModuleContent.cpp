// SPDX-License-Identifier: MIT
#include <interi/values/ModuleContent.h>

#include "JsonSchema.h"

#include <algorithm>
#include <format>

namespace interi::values
{

namespace
{

using detail::Invalid;

Result<const nlohmann::json*> GetSection(const nlohmann::json& root, std::string_view key)
{
    const auto it = root.find(key);
    if (it == root.end())
    {
        return nullptr;
    }
    if (!it->is_array())
    {
        return Invalid(std::format("\"{}\" must be an array", key));
    }
    return &*it;
}

/// Values and attributes: item id plus schema.
Result<void> ParseDefinitions(const nlohmann::json& root, std::string_view key, ModuleContent& content,
                              std::vector<ValueDefinition>& out)
{
    INTERI_TRY_ASSIGN(section, GetSection(root, key));
    if (section == nullptr)
    {
        return {};
    }
    const bool attributes = &out == &content.attributes;
    for (std::size_t i = 0; i < section->size(); ++i)
    {
        const nlohmann::json& entry = (*section)[i];
        const std::string where = std::format("{}[{}]", key, i);
        if (!entry.is_object())
        {
            return Invalid(std::format("{} is not an object", where));
        }
        ValueDefinition definition;
        INTERI_TRY_ASSIGN(id, detail::ParseItemId(entry, content.module, where, definition.isOverride));
        INTERI_TRY_ASSIGN(schema, detail::ParseSchema(entry, std::format("{} ('{}')", where, id)));
        INTERI_TRY_ASSIGN(version, detail::GetU32(entry, "version", 1, where));
        INTERI_TRY_ASSIGN(shared, detail::GetBool(entry, "shared", false, where));
        if (attributes && schema.type != ValueType::Bool && schema.type != ValueType::Int &&
            schema.type != ValueType::Float)
        {
            return Invalid(std::format("{} ('{}'): attributes are bool, int or float", where, id));
        }
        definition.id = std::move(id);
        definition.schema = std::move(schema);
        definition.schema.meta.shared = shared;
        definition.version = version;
        if (std::ranges::any_of(out, [&](const ValueDefinition& other) { return other.id == definition.id; }))
        {
            return Invalid(std::format("{}: '{}' is declared twice", where, definition.id));
        }
        detail::WarnUnknownKeys(entry, detail::kSchemaKeys, detail::kItemIdKeys, where, content.warnings);
        out.push_back(std::move(definition));
    }
    return {};
}

/// A required array of full item ids.
Result<std::vector<std::string>> GetIdList(const nlohmann::json& entry, std::string_view key, std::string_view where)
{
    const auto it = entry.find(key);
    if (it == entry.end() || !it->is_array())
    {
        return Invalid(std::format("{}: \"{}\" must be an array of ids", where, key));
    }
    std::vector<std::string> ids;
    for (const nlohmann::json& id : *it)
    {
        if (!id.is_string() || !(sdk::IsLocalId(id.get<std::string>()) || sdk::IsValidItemName(id.get<std::string>())))
        {
            return Invalid(std::format("{}: \"{}\" must be an array of ids", where, key));
        }
        if (std::ranges::find(ids, id.get<std::string>()) != ids.end())
        {
            return Invalid(std::format("{}: '{}' is listed twice", where, id.get<std::string>()));
        }
        ids.push_back(id.get<std::string>());
    }
    return ids;
}

constexpr std::string_view kSetKeys[] = {"attributes"};
constexpr std::string_view kArchetypeKeys[] = {"sets", "defaults"};

Result<void> ParseAttributeSets(const nlohmann::json& root, ModuleContent& content)
{
    INTERI_TRY_ASSIGN(section, GetSection(root, "attributeSets"));
    if (section == nullptr)
    {
        return {};
    }
    for (std::size_t i = 0; i < section->size(); ++i)
    {
        const nlohmann::json& entry = (*section)[i];
        const std::string where = std::format("attributeSets[{}]", i);
        if (!entry.is_object())
        {
            return Invalid(std::format("{} is not an object", where));
        }
        AttributeSetDefinition set;
        INTERI_TRY_ASSIGN(id, detail::ParseItemId(entry, content.module, where, set.isOverride));
        INTERI_TRY_ASSIGN(attributes, GetIdList(entry, "attributes", where));
        INTERI_TRY_ASSIGN(version, detail::GetU32(entry, "version", 1, where));
        set.id = std::move(id);
        set.attributes = std::move(attributes);
        set.version = version;
        if (std::ranges::any_of(content.attributeSets,
                                [&](const AttributeSetDefinition& other) { return other.id == set.id; }))
        {
            return Invalid(std::format("{}: '{}' is declared twice", where, set.id));
        }
        detail::WarnUnknownKeys(entry, kSetKeys, detail::kItemIdKeys, where, content.warnings);
        content.attributeSets.push_back(std::move(set));
    }
    return {};
}

Result<void> ParseArchetypes(const nlohmann::json& root, ModuleContent& content)
{
    INTERI_TRY_ASSIGN(section, GetSection(root, "archetypes"));
    if (section == nullptr)
    {
        return {};
    }
    for (std::size_t i = 0; i < section->size(); ++i)
    {
        const nlohmann::json& entry = (*section)[i];
        const std::string where = std::format("archetypes[{}]", i);
        if (!entry.is_object())
        {
            return Invalid(std::format("{} is not an object", where));
        }
        ArchetypeDefinition archetype;
        INTERI_TRY_ASSIGN(id, detail::ParseItemId(entry, content.module, where, archetype.isOverride));
        INTERI_TRY_ASSIGN(sets, GetIdList(entry, "sets", where));
        INTERI_TRY_ASSIGN(version, detail::GetU32(entry, "version", 1, where));
        archetype.id = std::move(id);
        archetype.sets = std::move(sets);
        archetype.version = version;
        if (const auto defaults = entry.find("defaults"); defaults != entry.end())
        {
            if (!defaults->is_object())
            {
                return Invalid(std::format("{}: \"defaults\" must be an object of attribute id -> value", where));
            }
            for (const auto& [attribute, value] : defaults->items())
            {
                archetype.defaults.emplace_back(attribute, value.dump());
            }
        }
        if (std::ranges::any_of(content.archetypes,
                                [&](const ArchetypeDefinition& other) { return other.id == archetype.id; }))
        {
            return Invalid(std::format("{}: '{}' is declared twice", where, archetype.id));
        }
        detail::WarnUnknownKeys(entry, kArchetypeKeys, detail::kItemIdKeys, where, content.warnings);
        content.archetypes.push_back(std::move(archetype));
    }
    return {};
}

} // namespace

Result<ModuleContent> ParseModuleContent(std::string_view moduleJson, std::string_view moduleId)
{
    const nlohmann::json root = nlohmann::json::parse(moduleJson, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid(std::format("{}: module.json is not a JSON object", moduleId));
    }
    ModuleContent content;
    content.module = std::string(moduleId);
    Result<void> parsed = ParseDefinitions(root, "values", content, content.values);
    if (parsed)
    {
        parsed = ParseDefinitions(root, "attributes", content, content.attributes);
    }
    if (parsed)
    {
        parsed = ParseAttributeSets(root, content);
    }
    if (parsed)
    {
        parsed = ParseArchetypes(root, content);
    }
    if (!parsed)
    {
        return Invalid(std::format("{}: {}", moduleId, parsed.error().message));
    }
    return content;
}

Result<ModuleContent> LoadModuleContent(const sdk::ModuleManifest& manifest)
{
    if (manifest.directory.empty())
    {
        ModuleContent content;
        content.module = manifest.id;
        return content;
    }
    INTERI_TRY_ASSIGN(text, detail::ReadTextFile((manifest.directory / "module.json").string()));
    return ParseModuleContent(text, manifest.id);
}

} // namespace interi::values
