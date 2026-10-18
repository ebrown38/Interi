// SPDX-License-Identifier: MIT
#pragma once

// Everything the modules of a project declare in their data sections, with the project's own data on top: Values
// (ValueRegistry), characters (CharacterRegistry) and data tables (DataTableRegistry). The module host owns one for the
// loaded project; the editor builds one from manifests.

#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/Characters.h>
#include <interi/values/DataTables.h>
#include <interi/values/ModuleContent.h>
#include <interi/values/ValueRegistry.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace interi::values
{

/// What one provider contributed, taken out for a rollback (DataRegistry::ExtractProvider).
struct ExtractedData
{
    ValueRegistry::Entries values;
    CharacterRegistry::Entries characters;
    DataTableRegistry::Entries tables;
};

/// Project data for ids no loaded module defines, kept as JSON text (by id) so saving the project doesn't lose it.
struct UnresolvedProjectData
{
    std::map<std::string, std::string, std::less<>> values;
    std::map<std::string, std::string, std::less<>> characters;
    std::map<std::string, std::string, std::less<>> tables;
};

class DataRegistry
{
public:
    /// `modules` must outlive the registry.
    explicit DataRegistry(const sdk::ModuleSet& modules);

    DataRegistry(const DataRegistry&) = delete;
    DataRegistry& operator=(const DataRegistry&) = delete;
    DataRegistry(DataRegistry&&) = delete;
    DataRegistry& operator=(DataRegistry&&) = delete;
    ~DataRegistry() = default;

    /// Adds a module's content (all or nothing). Call in load order.
    Result<void> AddModule(const ModuleContent& content);
    [[nodiscard]] ExtractedData ExtractProvider(std::string_view provider);
    void RemoveProvider(std::string_view provider) { (void)ExtractProvider(provider); }
    void Restore(ExtractedData data);

    /// Re-checks the project's data against the current definitions (after modules changed); one warning per item
    /// that no longer fits (it stays stored but unused).
    std::vector<std::string> Revalidate();

    /// Removes the whole project layer (values, archetype defaults, rows, unresolved entries).
    void ClearProject();
    [[nodiscard]] UnresolvedProjectData& Unresolved() noexcept { return m_unresolved; }
    [[nodiscard]] const UnresolvedProjectData& Unresolved() const noexcept { return m_unresolved; }
    /// The schema versions the project data was last loaded with (ProjectData.h), by "<section>/<id>".
    [[nodiscard]] std::map<std::string, std::uint32_t, std::less<>>& LoadedSchemaVersions() noexcept
    {
        return m_loadedVersions;
    }
    [[nodiscard]] const std::map<std::string, std::uint32_t, std::less<>>& LoadedSchemaVersions() const noexcept
    {
        return m_loadedVersions;
    }

    [[nodiscard]] ValueRegistry& Values() noexcept { return m_values; }
    [[nodiscard]] const ValueRegistry& Values() const noexcept { return m_values; }
    [[nodiscard]] CharacterRegistry& Characters() noexcept { return m_characters; }
    [[nodiscard]] const CharacterRegistry& Characters() const noexcept { return m_characters; }
    [[nodiscard]] DataTableRegistry& Tables() noexcept { return m_tables; }
    [[nodiscard]] const DataTableRegistry& Tables() const noexcept { return m_tables; }

private:
    ValueRegistry m_values;
    CharacterRegistry m_characters;
    DataTableRegistry m_tables;
    UnresolvedProjectData m_unresolved;
    std::map<std::string, std::uint32_t, std::less<>> m_loadedVersions;
};

} // namespace interi::values
