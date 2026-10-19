// SPDX-License-Identifier: MIT
#pragma once

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Scheduler.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/jobs/JobSystem.h>
#include <interi/nav/NavEvents.h>
#include <interi/platform/DynamicLibrary.h>
#include <interi/runtime/SaveGame.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/values/DataRegistry.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::hotreload
{

/// The provider id of a module loaded with ModuleHost::Load(path) (no manifest).
inline constexpr std::string_view kLocalModuleId = "local.module";

struct ModuleHostDesc
{
    /// Where loaded modules are copied before opening them (so a rebuild can overwrite the original, which Windows
    /// locks while it is loaded). Empty: a directory under the system temp directory.
    std::filesystem::path shadowDirectory;
    /// Searched for a manifest's library after the manifest's own directory (e.g. a build output directory).
    std::vector<std::filesystem::path> librarySearchPaths;
    /// Shared contract ids the engine declares (Extensions add their own). The Chain Reaction core library's
    /// (chain::CoreContracts) are always added.
    std::vector<std::string> engineContracts;
    /// Services offered to modules by name (ModuleRegistrar::Service); they must outlive the host.
    std::map<std::string, IModuleService*, std::less<>> services;
};

/// Per-load counters.
struct ModuleLoadStats
{
    std::uint32_t migratedComponents = 0;      ///< Component values converted to a new layout.
    std::uint32_t systems = 0;                 ///< Systems of the modules (re)loaded.
    std::uint32_t reloadedModules = 0;         ///< Modules (re)loaded.
    std::uint32_t migratedAttributeBlocks = 0; ///< AttributeBlocks rewritten for a changed archetype layout.
};

/// Loads a project's gameplay modules (see ModuleApi.h), runs their systems, and reloads them while the World keeps
/// its entities.
///
/// LoadProject resolves the module graph (interi/sdk/ModuleGraph.h), opens every library and registers the modules in
/// load order (dependencies first, otherwise bottom-up): first a module's module.json data sections into Data(), then
/// its code into the project's hooks. Reload(id) reloads a module and every loaded module that depends on it (data
/// re-read from module.json): their state is saved, the new instances are opened and registered next to the old ones
/// (dependencies first), entities whose module-owned components changed layout are migrated, AttributeBlocks are
/// migrated to their archetypes' new layouts, and only then are the old instances unloaded, dependents first. Any
/// failure (unresolvable project, missing library or entry point, ABI or toolchain mismatch, invalid data,
/// registration refused, a layout change without a migrator, a migrator refusing) leaves what was loaded before, and
/// the World, untouched.
///
/// Each module's systems run in a Scheduler of its own, in load order (RunSystems). Re-registered systems start with
/// no change history, so Changed filters match everything on their first run after a reload. Not thread-safe; call
/// between frames.
class ModuleHost
{
public:
    explicit ModuleHost(World& world, ModuleHostDesc desc = {});
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ModuleHost(ModuleHost&&) = delete;
    ModuleHost& operator=(ModuleHost&&) = delete;

    /// False when the engine was built without INTERI_BUILD_SHARED: modules can't share it, and every load fails
    /// with Unsupported.
    [[nodiscard]] static bool Supported() noexcept;

    /// Loads a project (`projectOrder`, top = highest priority) from the modules in `available`, replacing whatever
    /// is loaded. All or nothing.
    /// `physicsEngine` is the project's pin. The selected Physics Engine (and the engines it extends) load first,
    /// then each declaring module's adapter library for it, if it names one, after its main library; every module
    /// is told the engine's id and version (ModuleContext::physicsEngine).
    Result<ModuleLoadStats> LoadProject(std::span<const std::string> projectOrder,
                                        std::span<const sdk::ModuleManifest> available,
                                        const std::optional<sdk::PhysicsEngineRef>& physicsEngine = {});

    /// Loads (or reloads) the library at `path` as a project's only module, without a manifest (provider id
    /// kLocalModuleId). Fails if a project with manifests is loaded.
    Result<ModuleLoadStats> Load(const std::filesystem::path& path);

    /// Reloads module `id` from its library (rebuilt in place) and every loaded module that depends on it (a Physics
    /// Engine: every module built against it and every engine extending it).
    Result<ModuleLoadStats> Reload(std::string_view id);

    /// Unloads every module, dependents first.
    void Unload();

    [[nodiscard]] bool IsLoaded() const noexcept { return !m_modules.empty(); }
    /// Successful loads and reloads so far.
    [[nodiscard]] std::uint32_t Generation() const noexcept { return m_generation; }
    /// Loaded module ids in load order.
    [[nodiscard]] std::vector<std::string> LoadedModules() const;
    /// Every loaded module's system names, in load order, then registration order.
    [[nodiscard]] std::vector<std::string> SystemNames() const;

    /// The project's hooks (valid, and empty, while nothing is loaded).
    [[nodiscard]] sdk::HookRegistry& Hooks() noexcept { return *m_hooks; }
    [[nodiscard]] const sdk::HookRegistry& Hooks() const noexcept { return *m_hooks; }
    /// The loaded project's providers and shared contract ids.
    [[nodiscard]] const sdk::ModuleSet& Modules() const noexcept { return *m_moduleSet; }
    /// The loaded modules' data (module.json data sections) with the project's own data on top. Project data stays
    /// across loads and reloads; after each, it is re-checked (items that no longer fit are logged and unused).
    [[nodiscard]] values::DataRegistry& Data() noexcept { return *m_data; }
    [[nodiscard]] const values::DataRegistry& Data() const noexcept { return *m_data; }
    /// The project's Chain Reaction events and functions: the engine's core library plus what the loaded modules
    /// provide (removed before a module unloads; executors on it recompile). Valid while the host lives.
    [[nodiscard]] chain::ChainRegistry& Chains() noexcept { return *m_chains; }
    [[nodiscard]] const chain::ChainRegistry& Chains() const noexcept { return *m_chains; }
    /// The navigation mesh nav.FindPath reads. Set it between steps (nullptr: no mesh, every query is NotFound).
    [[nodiscard]] nav::NavSource& Navigation() noexcept { return *m_navigation; }
    /// The save sections the loaded modules registered (and the game's own): Capture/Apply them for save slots.
    /// A module's sections leave before its library unloads; a failed reload restores them.
    [[nodiscard]] SaveRegistry& Saves() noexcept { return m_saves; }
    [[nodiscard]] const SaveRegistry& Saves() const noexcept { return m_saves; }

    /// Runs every module's systems once, in load order (each module's then plays back `commands`).
    void RunSystems(JobSystem& jobs, CommandBuffers& commands);

private:
    struct Loaded;
    struct Plan;

    Result<std::unique_ptr<Loaded>> Open(const std::string& id, const std::filesystem::path& library,
                                         const std::filesystem::path& adapter = {});
    Result<void> OpenLibrary(const std::filesystem::path& library, DynamicLibrary& opened,
                             std::filesystem::path& shadowPath, const ModuleApi*& api);
    Result<void> Register(Loaded& module, std::span<const std::byte> savedState,
                          std::span<const std::pair<std::string, values::Value>> savedFields);
    [[nodiscard]] Result<std::filesystem::path> FindLibrary(const sdk::ModuleManifest& manifest,
                                                            std::string_view library) const;
    Result<ModuleLoadStats> Execute(Plan plan);
    static void LogUnload(const Loaded& module);
    [[nodiscard]] std::vector<std::pair<IModuleService*, std::shared_ptr<void>>>
    ExtractServiceEntries(std::string_view id);
    void DropServiceEntries(std::string_view id);

    World* m_world;
    ModuleHostDesc m_desc;
    std::unique_ptr<sdk::ModuleSet> m_moduleSet;
    std::unique_ptr<sdk::HookRegistry> m_hooks;
    std::unique_ptr<values::DataRegistry> m_data;
    std::unique_ptr<chain::ChainRegistry> m_chains;
    std::shared_ptr<nav::NavSource> m_navigation;
    SaveRegistry m_saves;
    sdk::ModuleGraph m_graph;
    std::vector<std::unique_ptr<Loaded>> m_modules; ///< In load order.
    std::uint32_t m_generation = 0;
    std::uint32_t m_shadowCounter = 0;
};

} // namespace interi::hotreload
