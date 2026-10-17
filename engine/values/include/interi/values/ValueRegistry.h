// SPDX-License-Identifier: MIT
#pragma once

// Module Values, layered: a project override beats the highest module's definition, which beats lower modules'.
// Definitions follow the identity rule of ContributionRegistry (a shared id's highest provider wins outright; a local
// id is replaced only by a declared override). The project layer is not a provider: it holds plain values, checked
// against whichever definition currently wins.

#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ModuleContent.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::values
{

/// The provider name Provenance reports for a project override.
inline constexpr std::string_view kProjectProviderId = "project";

/// A project override of one Value.
struct ProjectValue
{
    Value value;
    /// False while the value doesn't fit the winning definition (a module changed under it): Get falls back to the
    /// definition's default, but the value is kept and written back to the project.
    bool active = true;
};

/// Values of a project's modules plus the project's overrides. Not thread-safe to modify; concurrent reads are fine
/// while nothing modifies it.
class ValueRegistry
{
public:
    using Entries = std::vector<std::pair<std::string, sdk::ContributionRegistry<ValueDefinition>::Entry>>;

    /// `modules` must outlive the registry.
    explicit ValueRegistry(const sdk::ModuleSet& modules);

    ValueRegistry(const ValueRegistry&) = delete;
    ValueRegistry& operator=(const ValueRegistry&) = delete;
    ValueRegistry(ValueRegistry&&) = delete;
    ValueRegistry& operator=(ValueRegistry&&) = delete;
    ~ValueRegistry() = default;

    /// Adds one definition by `provider`: a Provide, or an Override for `definition.isOverride` (which must keep the
    /// owner's type: InvalidArgument otherwise). Project overrides of the id are re-checked.
    Result<void> Add(std::string_view provider, const ValueDefinition& definition);
    /// Adds every Value of a module's content (all or nothing).
    Result<void> AddModule(const ModuleContent& content);

    /// Removes and returns everything `provider` defined (for rollback with Restore).
    [[nodiscard]] Entries ExtractProvider(std::string_view provider);
    void Restore(Entries entries);

    /// The winning definition, or nullptr.
    [[nodiscard]] const ValueDefinition* Definition(std::string_view id) const noexcept;
    /// The effective value: the active project override, else the winning default. nullptr if undefined.
    [[nodiscard]] const Value* Get(std::string_view id) const noexcept;
    /// The effective value as T. NotFound if undefined, InvalidArgument if it holds another type.
    template <typename T>
    [[nodiscard]] Result<T> Get(std::string_view id) const
    {
        const Value* value = Get(id);
        if (value == nullptr)
        {
            return MakeError(ErrorCode::NotFound, std::string("no value '").append(id).append("'"));
        }
        const T* typed = std::get_if<T>(&value->Variant());
        if (typed == nullptr)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::string("value '").append(id).append("' is a ").append(ToString(TypeOf(*value))));
        }
        return *typed;
    }

    /// Sets the project's value of `id` (NotFound if undefined; InvalidArgument if it doesn't fit the definition).
    Result<void> SetProjectValue(std::string_view id, Value value);
    /// Stores a project value without checking it (loading a project: it becomes inactive if it doesn't fit).
    void LoadProjectValue(std::string_view id, Value value);
    /// Removes the project's value of `id`. False if there was none.
    bool ClearProjectValue(std::string_view id);
    [[nodiscard]] const ProjectValue* GetProjectValue(std::string_view id) const noexcept;
    [[nodiscard]] const std::map<std::string, ProjectValue, std::less<>>& ProjectValues() const noexcept
    {
        return m_project;
    }

    /// The active project override (provider kProjectProviderId) first, then every module definition, winner first.
    [[nodiscard]] std::vector<sdk::ProvenanceEntry> Provenance(std::string_view id) const;

    /// Every defined id, sorted.
    [[nodiscard]] std::vector<std::string> Ids() const { return m_definitions.Ids(); }

    /// Calls `callback(id)` when the effective value or definition of `id` may have changed.
    [[nodiscard]] sdk::Subscription Subscribe(std::function<void(std::string_view id)> callback);

    /// Re-checks every project value against the current definitions. Returns a warning for each one that became
    /// inactive.
    std::vector<std::string> Revalidate();

private:
    void Recheck(std::string_view id);

    sdk::ContributionRegistry<ValueDefinition> m_definitions;
    std::map<std::string, ProjectValue, std::less<>> m_project;
    sdk::detail::ChangeListeners m_listeners;
    sdk::Subscription m_forward;
};

} // namespace interi::values
