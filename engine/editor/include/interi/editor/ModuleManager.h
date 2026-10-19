// SPDX-License-Identifier: MIT
#pragma once

// The Module Manager's model (no UI): which installed modules are enabled, available or incompatible, what enabling,
// disabling or reordering one would change (checked against the module graph before anything is applied), and the
// trust a module's native code needs. Native code runs with full trust and no sandbox: see docs/module-trust.md.

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/sdk/Project.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// Modules the user has confirmed (the first time they enabled them). A module is trusted for one id, author and
/// license: a new author or license asks again.
class TrustStore
{
public:
    struct Entry
    {
        std::string id;
        std::string author;
        std::string license;
    };

    [[nodiscard]] bool IsTrusted(const sdk::ModuleManifest& manifest) const noexcept;
    void Trust(const sdk::ModuleManifest& manifest);
    void Revoke(std::string_view id);
    [[nodiscard]] const std::vector<Entry>& Entries() const noexcept { return m_entries; }

    /// A missing file is an empty store; malformed JSON is InvalidArgument.
    [[nodiscard]] static Result<TrustStore> Load(const std::filesystem::path& path);
    [[nodiscard]] Result<void> Save(const std::filesystem::path& path) const;
    [[nodiscard]] static Result<TrustStore> Parse(std::string_view json);
    [[nodiscard]] std::string Write() const;

private:
    std::vector<Entry> m_entries; ///< Sorted by id.
};

/// Where the trust store lives next to the preferences file (`trusted_modules.json`).
[[nodiscard]] std::filesystem::path TrustStorePath(const std::filesystem::path& preferencesPath);

/// True if the module ships native code (a library) the store hasn't confirmed.
[[nodiscard]] bool NeedsTrust(const sdk::ModuleManifest& manifest, const TrustStore& store) noexcept;

enum class ModuleStatus : std::uint8_t
{
    Enabled,      ///< In the project's list.
    Implicit,     ///< Pulled in as a dependency of an enabled module.
    Available,    ///< Installed, and could be enabled.
    Incompatible, ///< Installed, but the engine or the project refuses it (`reason`).
    Engine,       ///< A Physics Engine: selected from the modules' `physicsEngines`, never enabled by hand.
};

[[nodiscard]] std::string_view ToString(ModuleStatus status) noexcept;

struct ModuleEntry
{
    std::string id;
    std::string name;
    std::string author;
    std::string license;
    std::string description;
    sdk::SemVer version;
    sdk::ModuleKind kind = sdk::ModuleKind::Module;
    ModuleStatus status = ModuleStatus::Available;
    /// Position in the project's list (0 = top = highest priority) for Enabled modules, else -1.
    int position = -1;
    /// Incompatible: why. Implicit: "required by ...". Engine: whether it is the selected one.
    std::string reason;
    bool nativeCode = false;
    bool trusted = false;
    /// The loaded modules that require this one.
    std::vector<std::string> requiredBy;
};

struct ModuleManagerModel
{
    /// Enabled (project order), Implicit (load order), Engine, Available (by id), Incompatible (by id).
    std::vector<ModuleEntry> entries;
    /// Set when the project doesn't resolve as it is (everything installed is then listed Available or Incompatible).
    std::string graphError;

    [[nodiscard]] const ModuleEntry* Find(std::string_view id) const noexcept;
};

[[nodiscard]] ModuleManagerModel BuildModuleManagerModel(const sdk::Project& project,
                                                         std::span<const sdk::ModuleManifest> manifests,
                                                         const sdk::HostInfo& host, const TrustStore& trust);

/// What a change to the project's modules does, resolved against the module graph.
struct ModuleChange
{
    sdk::Project project;             ///< The project with the change; the input project is never modified.
    std::vector<std::string> added;   ///< Modules loaded after the change that weren't before (implicit ones too).
    std::vector<std::string> removed; ///< Modules loaded before the change that no longer are.
    std::string physicsEngineBefore;  ///< Empty: none.
    std::string physicsEngineAfter;
    /// Added modules whose native code the store hasn't confirmed (the explicit one, implicit Extensions, a newly
    /// selected Physics Engine): the UI asks before applying.
    std::vector<std::string> needsTrust;

    [[nodiscard]] bool PhysicsEngineChanged() const noexcept { return physicsEngineBefore != physicsEngineAfter; }
};

/// Enable `id` at `position` in the project's list (0 = top). Refused (the reason as the error) when it isn't
/// installed, is a Physics Engine or is enabled already, or when the project with it wouldn't resolve.
[[nodiscard]] Result<ModuleChange> EnableModule(const sdk::Project& project,
                                                std::span<const sdk::ModuleManifest> manifests, std::string_view id,
                                                const sdk::HostInfo& host, const TrustStore& trust,
                                                std::size_t position = 0);

/// Disable `id`. Refused when it isn't in the project's list, when another loaded module requires it ("required by
/// A, B": disable those first), or when the rest wouldn't resolve.
[[nodiscard]] Result<ModuleChange> DisableModule(const sdk::Project& project,
                                                 std::span<const sdk::ModuleManifest> manifests, std::string_view id,
                                                 const sdk::HostInfo& host, const TrustStore& trust);

/// Move `id` to `position` (clamped to the list; 0 = highest priority). Refused when the reordered project wouldn't
/// resolve (e.g. an engine choice that depends on priority).
[[nodiscard]] Result<ModuleChange> MoveModule(const sdk::Project& project,
                                              std::span<const sdk::ModuleManifest> manifests, std::string_view id,
                                              std::size_t position, const sdk::HostInfo& host, const TrustStore& trust);

} // namespace interi::editor
