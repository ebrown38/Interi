// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/ecs/Query.h>
#include <interi/values/Characters.h>

#include "ValueJson.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

namespace interi::values
{

namespace
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

std::uint32_t ToBits(const Value& value) noexcept
{
    if (const auto* flag = std::get_if<bool>(&value.Variant()))
    {
        return *flag ? 1u : 0u;
    }
    if (const auto* integer = std::get_if<std::int32_t>(&value.Variant()))
    {
        return std::bit_cast<std::uint32_t>(*integer);
    }
    const auto* real = std::get_if<float>(&value.Variant());
    return real == nullptr ? 0u : std::bit_cast<std::uint32_t>(*real);
}

Value FromBits(ValueType type, std::uint32_t bits) noexcept
{
    switch (type)
    {
    case ValueType::Bool:
        return bits != 0;
    case ValueType::Int:
        return std::bit_cast<std::int32_t>(bits);
    default:
        return std::bit_cast<float>(bits);
    }
}

/// Converts between bool, int and float and clamps to the schema's range.
Value Convert(const Value& from, const ValueSchema& to)
{
    double number = 0.0;
    if (const auto* flag = std::get_if<bool>(&from.Variant()))
    {
        number = *flag ? 1.0 : 0.0;
    }
    else if (const auto* integer = std::get_if<std::int32_t>(&from.Variant()))
    {
        number = static_cast<double>(*integer);
    }
    else if (const auto* real = std::get_if<float>(&from.Variant()))
    {
        number = std::isfinite(*real) ? static_cast<double>(*real) : 0.0;
    }
    if (to.meta.min)
    {
        number = std::max(number, *to.meta.min);
    }
    if (to.meta.max)
    {
        number = std::min(number, *to.meta.max);
    }
    switch (to.type)
    {
    case ValueType::Bool:
        return number != 0.0;
    case ValueType::Int:
        return static_cast<std::int32_t>(std::clamp(std::trunc(number), -2147483648.0, 2147483647.0));
    default:
        return static_cast<float>(number);
    }
}

const AttributeSlot* FindSlot(const AttributeBlock& block, std::uint64_t key) noexcept
{
    const auto end =
        block.slots.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(block.count, kMaxAttributes));
    const auto found =
        std::find_if(block.slots.begin(), end, [key](const AttributeSlot& slot) { return slot.key == key; });
    return found == end ? nullptr : &*found;
}

template <typename T>
Result<void> AddAll(sdk::ContributionRegistry<T>& registry, std::string_view provider, const std::vector<T>& items)
{
    for (const T& item : items)
    {
        const Result<void> added =
            item.isOverride ? registry.Override(provider, item.id, item) : registry.Provide(provider, item.id, item);
        if (!added)
        {
            return added;
        }
    }
    return {};
}

} // namespace

std::uint64_t AttributeKey(std::string_view id) noexcept
{
    return Hash64(id);
}

std::size_t AttributeLayout::Find(std::string_view id) const noexcept
{
    const auto found = std::ranges::find(slots, id, &AttributeSlotInfo::id);
    return found == slots.end() ? npos : static_cast<std::size_t>(found - slots.begin());
}

Result<AttributeBlock> MakeAttributeBlock(const AttributeLayout& layout,
                                          std::span<const std::pair<std::string, Value>> overrides)
{
    if (layout.slots.size() > kMaxAttributes)
    {
        return Invalid(std::format("archetype '{}' has more than {} attributes", layout.archetype, kMaxAttributes));
    }
    AttributeBlock block;
    block.archetype = layout.archetypeKey;
    block.layoutVersion = layout.version;
    block.count = static_cast<std::uint32_t>(layout.slots.size());
    for (std::size_t i = 0; i < layout.slots.size(); ++i)
    {
        const AttributeSlotInfo& info = layout.slots[i];
        block.slots[i].key = info.key;
        block.slots[i].type = info.schema.type;
        block.slots[i].bits = ToBits(info.schema.defaultValue);
    }
    for (const auto& [id, value] : overrides)
    {
        INTERI_TRY(SetAttribute(block, layout, id, value));
    }
    return block;
}

std::optional<Value> GetAttribute(const AttributeBlock& block, std::string_view id) noexcept
{
    const AttributeSlot* slot = FindSlot(block, AttributeKey(id));
    if (slot == nullptr)
    {
        return std::nullopt;
    }
    return FromBits(slot->type, slot->bits);
}

Result<void> SetAttribute(AttributeBlock& block, const AttributeLayout& layout, std::string_view id, const Value& value)
{
    const std::size_t index = layout.Find(id);
    if (index == AttributeLayout::npos)
    {
        return MakeError(ErrorCode::NotFound,
                         std::format("archetype '{}' has no attribute '{}'", layout.archetype, id));
    }
    const AttributeSlotInfo& info = layout.slots[index];
    if (const Result<void> valid = ValidateValue(info.schema, value); !valid)
    {
        return Invalid(std::format("attribute '{}': {}", id, valid.error().message));
    }
    auto* slot = const_cast<AttributeSlot*>(FindSlot(block, info.key));
    if (slot == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("the block has no attribute '{}' (migrate it first)", id));
    }
    slot->bits = ToBits(value);
    return {};
}

bool MigrateAttributeBlock(AttributeBlock& block, const AttributeLayout& layout)
{
    if (block.layoutVersion == layout.version && block.archetype == layout.archetypeKey)
    {
        return false;
    }
    AttributeBlock migrated = MakeAttributeBlock(layout).value();
    for (std::size_t i = 0; i < layout.slots.size(); ++i)
    {
        if (const AttributeSlot* old = FindSlot(block, layout.slots[i].key))
        {
            migrated.slots[i].bits = ToBits(Convert(FromBits(old->type, old->bits), layout.slots[i].schema));
        }
    }
    block = migrated;
    return true;
}

CharacterRegistry::CharacterRegistry(const sdk::ModuleSet& modules)
    : m_attributes(modules)
    , m_sets(modules)
    , m_archetypes(modules)
{
}

Result<void> CharacterRegistry::AddModule(const ModuleContent& content)
{
    Result<void> added;
    for (const AttributeDefinition& attribute : content.attributes)
    {
        if (!attribute.isOverride)
        {
            continue;
        }
        const std::string_view owner = sdk::OwnerOf(attribute.id);
        for (const auto& entry : m_attributes.Stack(attribute.id))
        {
            if (entry.provider == owner && entry.value.schema.type != attribute.schema.type)
            {
                added = Invalid(std::format("'{}' overrides attribute '{}' with type {}, but its owner declares {}",
                                            content.module, attribute.id, ToString(attribute.schema.type),
                                            ToString(entry.value.schema.type)));
            }
        }
    }
    if (added)
    {
        added = AddAll(m_attributes, content.module, content.attributes);
    }
    if (added)
    {
        added = AddAll(m_sets, content.module, content.attributeSets);
    }
    if (added)
    {
        added = AddAll(m_archetypes, content.module, content.archetypes);
    }
    if (!added)
    {
        (void)ExtractProvider(content.module);
    }
    return added;
}

CharacterRegistry::Entries CharacterRegistry::ExtractProvider(std::string_view provider)
{
    Entries entries;
    entries.attributes = m_attributes.ExtractProvider(provider);
    entries.sets = m_sets.ExtractProvider(provider);
    entries.archetypes = m_archetypes.ExtractProvider(provider);
    return entries;
}

void CharacterRegistry::Restore(Entries entries)
{
    m_attributes.Restore(std::move(entries.attributes));
    m_sets.Restore(std::move(entries.sets));
    m_archetypes.Restore(std::move(entries.archetypes));
}

const AttributeDefinition* CharacterRegistry::Attribute(std::string_view id) const noexcept
{
    return m_attributes.Resolve(id);
}

const AttributeSetDefinition* CharacterRegistry::AttributeSet(std::string_view id) const noexcept
{
    return m_sets.Resolve(id);
}

const ArchetypeDefinition* CharacterRegistry::Archetype(std::string_view id) const noexcept
{
    return m_archetypes.Resolve(id);
}

const ArchetypeDefinition* CharacterRegistry::ArchetypeByKey(std::uint64_t key) const noexcept
{
    for (const std::string& id : m_archetypes.Ids())
    {
        if (AttributeKey(id) == key)
        {
            return m_archetypes.Resolve(id);
        }
    }
    return nullptr;
}

Result<Value> CharacterRegistry::ArchetypeDefault(const ArchetypeDefinition& archetype, std::string_view attribute,
                                                  const ValueSchema& schema) const
{
    for (const auto& [id, json] : archetype.defaults)
    {
        if (id != attribute)
        {
            continue;
        }
        const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
        INTERI_TRY_ASSIGN(
            value, detail::ValueFromJson(parsed, schema, std::format("archetype '{}' default '{}'", archetype.id, id)));
        if (const Result<void> valid = ValidateValue(schema, value); !valid)
        {
            return Invalid(std::format("archetype '{}' default '{}': {}", archetype.id, id, valid.error().message));
        }
        return value;
    }
    return schema.defaultValue;
}

Result<AttributeLayout> CharacterRegistry::Layout(std::string_view archetypeId) const
{
    const ArchetypeDefinition* archetype = Archetype(archetypeId);
    if (archetype == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no archetype '{}'", archetypeId));
    }
    AttributeLayout layout;
    layout.archetype = archetype->id;
    layout.archetypeKey = AttributeKey(archetype->id);
    std::uint64_t version = layout.archetypeKey;
    const auto projectIt = m_projectDefaults.find(archetype->id);
    for (const std::string& setId : archetype->sets)
    {
        const AttributeSetDefinition* set = AttributeSet(setId);
        if (set == nullptr)
        {
            return MakeError(ErrorCode::NotFound,
                             std::format("archetype '{}': no attribute set '{}'", archetype->id, setId));
        }
        for (const std::string& attributeId : set->attributes)
        {
            if (layout.Find(attributeId) != AttributeLayout::npos)
            {
                continue;
            }
            const AttributeDefinition* attribute = Attribute(attributeId);
            if (attribute == nullptr)
            {
                return MakeError(ErrorCode::NotFound,
                                 std::format("attribute set '{}': no attribute '{}'", set->id, attributeId));
            }
            AttributeSlotInfo slot{.id = attributeId, .key = AttributeKey(attributeId), .schema = attribute->schema};
            INTERI_TRY_ASSIGN(value, ArchetypeDefault(*archetype, attributeId, slot.schema));
            slot.schema.defaultValue = std::move(value);
            if (projectIt != m_projectDefaults.end())
            {
                const auto project = projectIt->second.find(attributeId);
                if (project != projectIt->second.end() && ValidateValue(slot.schema, project->second))
                {
                    slot.schema.defaultValue = project->second;
                }
            }
            for (const AttributeSlotInfo& other : layout.slots)
            {
                if (other.key == slot.key)
                {
                    return Invalid(std::format("attributes '{}' and '{}' have the same key", other.id, attributeId));
                }
            }
            version = HashCombine(HashCombine(version, slot.key), static_cast<std::uint64_t>(slot.schema.type));
            layout.slots.push_back(std::move(slot));
        }
    }
    for (const auto& [id, json] : archetype->defaults)
    {
        if (layout.Find(id) == AttributeLayout::npos)
        {
            return Invalid(
                std::format("archetype '{}' sets a default for '{}', which none of its sets has", archetype->id, id));
        }
    }
    if (layout.slots.size() > kMaxAttributes)
    {
        return Invalid(std::format("archetype '{}' has {} attributes (at most {})", archetype->id, layout.slots.size(),
                                   kMaxAttributes));
    }
    layout.version = version;
    return layout;
}

Result<void> CharacterRegistry::CheckProjectDefault(std::string_view archetype, std::string_view attribute,
                                                    const Value& value) const
{
    INTERI_TRY_ASSIGN(layout, Layout(archetype));
    const std::size_t index = layout.Find(attribute);
    if (index == AttributeLayout::npos)
    {
        return MakeError(ErrorCode::NotFound,
                         std::format("archetype '{}' has no attribute '{}'", archetype, attribute));
    }
    if (const Result<void> valid = ValidateValue(layout.slots[index].schema, value); !valid)
    {
        return Invalid(std::format("archetype '{}' attribute '{}': {}", archetype, attribute, valid.error().message));
    }
    return {};
}

Result<void> CharacterRegistry::SetProjectDefault(std::string_view archetype, std::string_view attribute, Value value)
{
    INTERI_TRY(CheckProjectDefault(archetype, attribute, value));
    LoadProjectDefault(archetype, attribute, std::move(value));
    return {};
}

void CharacterRegistry::LoadProjectDefault(std::string_view archetype, std::string_view attribute, Value value)
{
    auto it = m_projectDefaults.find(archetype);
    if (it == m_projectDefaults.end())
    {
        it = m_projectDefaults.emplace(std::string(archetype), std::map<std::string, Value, std::less<>>{}).first;
    }
    it->second.insert_or_assign(std::string(attribute), std::move(value));
}

bool CharacterRegistry::ClearProjectDefault(std::string_view archetype, std::string_view attribute)
{
    const auto it = m_projectDefaults.find(archetype);
    if (it == m_projectDefaults.end())
    {
        return false;
    }
    const auto attributeIt = it->second.find(attribute);
    if (attributeIt == it->second.end())
    {
        return false;
    }
    it->second.erase(attributeIt);
    if (it->second.empty())
    {
        m_projectDefaults.erase(it);
    }
    return true;
}

const Value* CharacterRegistry::ProjectDefault(std::string_view archetype, std::string_view attribute) const noexcept
{
    const auto it = m_projectDefaults.find(archetype);
    if (it == m_projectDefaults.end())
    {
        return nullptr;
    }
    const auto attributeIt = it->second.find(attribute);
    return attributeIt == it->second.end() ? nullptr : &attributeIt->second;
}

std::vector<std::string> CharacterRegistry::Revalidate() const
{
    std::vector<std::string> warnings;
    for (const auto& [archetype, attributes] : m_projectDefaults)
    {
        for (const auto& [attribute, value] : attributes)
        {
            if (const Result<void> valid = CheckProjectDefault(archetype, attribute, value); !valid)
            {
                warnings.push_back(std::format("project default {} = {}: {}; using the module default", attribute,
                                               FormatValue(value), valid.error().message));
            }
        }
    }
    return warnings;
}

std::uint32_t CharacterRegistry::MigrateBlocks(World& world) const
{
    std::map<std::uint64_t, std::optional<AttributeLayout>> layouts;
    std::uint32_t migrated = 0;
    Query query = Query{}.With<AttributeBlock>();
    query.ForEachChunk(world,
                       [&](ChunkView& chunk)
                       {
                           for (AttributeBlock& block : chunk.Write<AttributeBlock>())
                           {
                               auto [it, inserted] = layouts.try_emplace(block.archetype);
                               if (inserted)
                               {
                                   if (const ArchetypeDefinition* archetype = ArchetypeByKey(block.archetype))
                                   {
                                       Result<AttributeLayout> layout = Layout(archetype->id);
                                       if (layout)
                                       {
                                           it->second = std::move(*layout);
                                       }
                                   }
                               }
                               if (it->second && MigrateAttributeBlock(block, *it->second))
                               {
                                   ++migrated;
                               }
                           }
                       });
    return migrated;
}

} // namespace interi::values
