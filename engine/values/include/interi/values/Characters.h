// SPDX-License-Identifier: MIT
#pragma once

// Characters: attribute definitions, attribute sets and archetypes from the modules (each a layered registry item
// under the identity rule), the project's per-archetype default overrides, and the runtime AttributeBlock component.
//
// An archetype's AttributeLayout lists its attributes (the sets' attributes in set order, duplicates dropped) with
// their resolved schemas and defaults: attribute default < archetype default < project override. AttributeBlock is
// self-describing (each slot carries the attribute's key and type), so a block written under an older layout is
// migrated by attribute key alone: kept attributes keep their value (converted between bool/int/float and clamped to
// the new range), new ones take the default, removed ones are dropped.

#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ModuleContent.h>
#include <interi/values/Value.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::values
{

/// Attribute slots per AttributeBlock.
inline constexpr std::size_t kMaxAttributes = 32;

/// The key of an attribute or archetype id in an AttributeBlock.
[[nodiscard]] std::uint64_t AttributeKey(std::string_view id) noexcept;

/// One attribute of a layout.
struct AttributeSlotInfo
{
    std::string id;
    std::uint64_t key = 0;
    ValueSchema schema; ///< The winning definition's, with the archetype's (and project's) default.
};

/// The resolved attributes of one archetype.
struct AttributeLayout
{
    std::string archetype;
    std::uint64_t archetypeKey = 0;
    std::uint64_t version = 0; ///< Hash of the attribute keys and types: a block with another version is migrated.
    std::vector<AttributeSlotInfo> slots;

    /// Index into `slots`, or npos.
    [[nodiscard]] std::size_t Find(std::string_view id) const noexcept;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
};

struct AttributeSlot
{
    std::uint64_t key = 0;
    std::uint32_t bits = 0; ///< bool 0/1, int32 or float bit pattern.
    ValueType type = ValueType::Bool;
    std::uint8_t padding[3] = {};
};

/// A character's attributes (engine component; trivially copyable, so it survives module reloads unchanged).
struct AttributeBlock
{
    static constexpr const char* kComponentName = "interi.values.AttributeBlock";

    std::uint64_t archetype = 0;     ///< AttributeKey of the archetype id.
    std::uint64_t layoutVersion = 0; ///< AttributeLayout::version it was written with.
    std::uint32_t count = 0;
    std::array<AttributeSlot, kMaxAttributes> slots{};
};

/// A new block with the layout's defaults, then `overrides` (per-entity values; InvalidArgument if one names no
/// attribute of the layout or doesn't fit its schema).
[[nodiscard]] Result<AttributeBlock> MakeAttributeBlock(const AttributeLayout& layout,
                                                        std::span<const std::pair<std::string, Value>> overrides = {});

/// The attribute's value, or nullopt if the block has none with that id.
[[nodiscard]] std::optional<Value> GetAttribute(const AttributeBlock& block, std::string_view id) noexcept;

/// Typed read: NotFound without the attribute, InvalidArgument for another type.
template <typename T>
[[nodiscard]] Result<T> GetAttribute(const AttributeBlock& block, std::string_view id)
{
    const std::optional<Value> value = GetAttribute(block, id);
    if (!value)
    {
        return MakeError(ErrorCode::NotFound, std::string("no attribute '").append(id).append("'"));
    }
    const T* typed = std::get_if<T>(&value->Variant());
    if (typed == nullptr)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::string("attribute '").append(id).append("' is a ").append(ToString(TypeOf(*value))));
    }
    return *typed;
}

/// Writes an attribute (validated against the layout's schema).
Result<void> SetAttribute(AttributeBlock& block, const AttributeLayout& layout, std::string_view id,
                          const Value& value);

/// Rewrites a block written under another version of `layout` (see the top of this header). False if it already
/// matches. The block must belong to the layout's archetype.
bool MigrateAttributeBlock(AttributeBlock& block, const AttributeLayout& layout);

/// Attributes and archetypes of a project's modules, plus the project's archetype default overrides.
class CharacterRegistry
{
public:
    struct Entries
    {
        std::vector<std::pair<std::string, sdk::ContributionRegistry<AttributeDefinition>::Entry>> attributes;
        std::vector<std::pair<std::string, sdk::ContributionRegistry<AttributeSetDefinition>::Entry>> sets;
        std::vector<std::pair<std::string, sdk::ContributionRegistry<ArchetypeDefinition>::Entry>> archetypes;
    };

    explicit CharacterRegistry(const sdk::ModuleSet& modules);

    CharacterRegistry(const CharacterRegistry&) = delete;
    CharacterRegistry& operator=(const CharacterRegistry&) = delete;
    CharacterRegistry(CharacterRegistry&&) = delete;
    CharacterRegistry& operator=(CharacterRegistry&&) = delete;
    ~CharacterRegistry() = default;

    /// Adds a module's attributes, sets and archetypes (all or nothing). Overrides keep the owner's attribute type.
    Result<void> AddModule(const ModuleContent& content);
    [[nodiscard]] Entries ExtractProvider(std::string_view provider);
    void Restore(Entries entries);

    [[nodiscard]] const AttributeDefinition* Attribute(std::string_view id) const noexcept;
    [[nodiscard]] const AttributeSetDefinition* AttributeSet(std::string_view id) const noexcept;
    [[nodiscard]] const ArchetypeDefinition* Archetype(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<std::string> ArchetypeIds() const { return m_archetypes.Ids(); }
    [[nodiscard]] std::vector<std::string> AttributeIds() const { return m_attributes.Ids(); }
    [[nodiscard]] std::vector<sdk::ProvenanceEntry> AttributeProvenance(std::string_view id) const
    {
        return m_attributes.Provenance(id);
    }
    [[nodiscard]] std::vector<sdk::ProvenanceEntry> ArchetypeProvenance(std::string_view id) const
    {
        return m_archetypes.Provenance(id);
    }

    /// Resolves an archetype. NotFound for an unknown archetype, set or attribute; InvalidArgument for a default that
    /// doesn't fit its attribute or more than kMaxAttributes attributes. Invalid project overrides are skipped.
    [[nodiscard]] Result<AttributeLayout> Layout(std::string_view archetype) const;
    /// The archetype with this AttributeKey, or nullptr.
    [[nodiscard]] const ArchetypeDefinition* ArchetypeByKey(std::uint64_t key) const noexcept;

    /// The project's default for one attribute of an archetype (validated: NotFound / InvalidArgument).
    Result<void> SetProjectDefault(std::string_view archetype, std::string_view attribute, Value value);
    /// Stores one without checking it (loading a project; Layout skips it while it doesn't fit).
    void LoadProjectDefault(std::string_view archetype, std::string_view attribute, Value value);
    bool ClearProjectDefault(std::string_view archetype, std::string_view attribute);
    [[nodiscard]] const Value* ProjectDefault(std::string_view archetype, std::string_view attribute) const noexcept;
    /// archetype -> attribute -> value.
    [[nodiscard]] const std::map<std::string, std::map<std::string, Value, std::less<>>, std::less<>>&
    ProjectDefaults() const noexcept
    {
        return m_projectDefaults;
    }

    /// One warning per project default that doesn't fit (unknown archetype or attribute, wrong type, out of range).
    [[nodiscard]] std::vector<std::string> Revalidate() const;

    /// Migrates every AttributeBlock in `world` to its archetype's current layout. Returns the blocks changed;
    /// blocks of unknown archetypes are left alone.
    std::uint32_t MigrateBlocks(World& world) const;

private:
    [[nodiscard]] Result<Value> ArchetypeDefault(const ArchetypeDefinition& archetype, std::string_view attribute,
                                                 const ValueSchema& schema) const;
    [[nodiscard]] Result<void> CheckProjectDefault(std::string_view archetype, std::string_view attribute,
                                                   const Value& value) const;

    sdk::ContributionRegistry<AttributeDefinition> m_attributes;
    sdk::ContributionRegistry<AttributeSetDefinition> m_sets;
    sdk::ContributionRegistry<ArchetypeDefinition> m_archetypes;
    std::map<std::string, std::map<std::string, Value, std::less<>>, std::less<>> m_projectDefaults;
};

} // namespace interi::values
