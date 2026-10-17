// SPDX-License-Identifier: MIT
#pragma once

// Everything the modules of a project declare in their data sections, with the project's own data on top: Values
// (ValueRegistry) and characters (CharacterRegistry). The module host owns one for the loaded project; the editor
// builds one from manifests.

#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/Characters.h>
#include <interi/values/ModuleContent.h>
#include <interi/values/ValueRegistry.h>

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

    [[nodiscard]] ValueRegistry& Values() noexcept { return m_values; }
    [[nodiscard]] const ValueRegistry& Values() const noexcept { return m_values; }
    [[nodiscard]] CharacterRegistry& Characters() noexcept { return m_characters; }
    [[nodiscard]] const CharacterRegistry& Characters() const noexcept { return m_characters; }

private:
    ValueRegistry m_values;
    CharacterRegistry m_characters;
};

} // namespace interi::values
