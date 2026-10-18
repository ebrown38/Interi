// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/core/Log.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/platform/DynamicLibrary.h>
#include <interi/sdk/Toolchain.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace interi::hotreload
{

using HookEntries = std::vector<std::pair<std::string, sdk::HookEntry>>;

struct ModuleHost::Loaded
{
    std::string id;
    std::filesystem::path libraryPath; ///< The original (rebuilt in place), empty for a content-only module.
    DynamicLibrary library;
    std::filesystem::path shadowPath;
    const ModuleApi* api = nullptr;
    bool attached = false; ///< api->load was called, so api->unload must be.
    std::unique_ptr<sdk::ModuleScope> scope;
    std::unique_ptr<Scheduler> scheduler = std::make_unique<Scheduler>();
    std::vector<std::string> systemNames; ///< By Scheduler::SystemId.
    std::vector<std::pair<ComponentTypeId, ComponentMigrator>> components;
    StateSaver saver;
    void* stateObject = nullptr; ///< DeclareState: module memory, valid while the module is loaded.
    std::vector<values::FieldInfo> stateFields;
    values::ModuleContent content; ///< module.json data sections.

    /// The declared state's fields by name (read before the module is unloaded for a reload).
    [[nodiscard]] std::vector<std::pair<std::string, values::Value>> ReadState() const
    {
        std::vector<std::pair<std::string, values::Value>> state;
        state.reserve(stateFields.size());
        for (const values::FieldInfo& field : stateFields)
        {
            state.emplace_back(field.name, values::ReadField(field, stateObject));
        }
        return state;
    }

    Loaded() = default;
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    Loaded(Loaded&&) = delete;
    Loaded& operator=(Loaded&&) = delete;

    /// The module's hooks must already be out of the registry: they are module code too.
    ~Loaded()
    {
        // The systems (and whatever they captured) are module code: destroy them while the module is still loaded.
        scheduler.reset();
        saver = nullptr;
        if (attached)
        {
            api->unload();
        }
        library.Close();
        if (!shadowPath.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(shadowPath, ignored);
        }
    }
};

namespace
{

class Registrar final : public ModuleRegistrar
{
public:
    Registrar(const sdk::ModuleScope& scope, const values::DataRegistry& data, chain::ChainRegistry& chains,
              std::span<const std::byte> savedState, std::vector<std::string>& systemNames, Scheduler& scheduler,
              std::vector<std::pair<ComponentTypeId, ComponentMigrator>>& components, StateSaver& saver,
              std::span<const std::pair<std::string, values::Value>> savedFields, void*& stateObject,
              std::vector<values::FieldInfo>& stateFields)
        : m_scope(&scope)
        , m_data(&data)
        , m_chains(&chains)
        , m_savedState(savedState)
        , m_systemNames(&systemNames)
        , m_scheduler(&scheduler)
        , m_components(&components)
        , m_saver(&saver)
        , m_savedFields(savedFields)
        , m_stateObject(&stateObject)
        , m_stateFields(&stateFields)
    {
    }
    Registrar(const Registrar&) = delete;
    Registrar& operator=(const Registrar&) = delete;
    Registrar(Registrar&&) = delete;
    Registrar& operator=(Registrar&&) = delete;
    ~Registrar() = default;

    void DeclareComponent(ComponentTypeId type, ComponentMigrator migrator) override
    {
        m_components->emplace_back(type, migrator);
    }

    void AddSystem(std::string_view name, SystemAccess access, Scheduler::SystemFunction function) override
    {
        if (Find(name).has_value())
        {
            Fail(std::format("duplicate system name '{}'", name));
            return;
        }
        (void)m_scheduler->AddSystem(std::string(name), access, std::move(function));
        m_systemNames->emplace_back(name);
    }

    void RunAfter(std::string_view system, std::string_view dependency) override
    {
        m_orders.emplace_back(system, dependency);
    }

    [[nodiscard]] const sdk::ModuleScope& Scope() const override { return *m_scope; }
    [[nodiscard]] const values::DataRegistry& Data() const override { return *m_data; }
    [[nodiscard]] chain::ChainRegistry& Chains() const override { return *m_chains; }
    [[nodiscard]] std::span<const std::byte> SavedState() const override { return m_savedState; }
    void SetStateSaver(StateSaver saver) override { *m_saver = std::move(saver); }

    void DeclareState(void* object, std::vector<values::FieldInfo> fields) override
    {
        if (object == nullptr || *m_stateObject != nullptr)
        {
            Fail("DeclareState needs an object and may be called once");
            return;
        }
        for (const values::FieldInfo& field : fields)
        {
            const auto saved =
                std::ranges::find(m_savedFields, field.name, &std::pair<std::string, values::Value>::first);
            if (saved != m_savedFields.end())
            {
                (void)values::WriteField(field, object, saved->second); // false (kept) if the type changed
            }
        }
        *m_stateObject = object;
        *m_stateFields = std::move(fields);
    }

    /// Applies the RunAfter orders (systems may be named before they are added) and builds the scheduler.
    Result<void> Finish()
    {
        for (const auto& [system, dependency] : m_orders)
        {
            const std::optional<Scheduler::SystemId> after = Find(system);
            const std::optional<Scheduler::SystemId> before = Find(dependency);
            if (!after || !before)
            {
                Fail(std::format("RunAfter('{}', '{}') names an unknown system", system, dependency));
                continue;
            }
            m_scheduler->RunAfter(*after, *before);
        }
        if (!m_error.empty())
        {
            return MakeError(ErrorCode::InvalidArgument, m_error);
        }
        return m_scheduler->Build();
    }

private:
    [[nodiscard]] std::optional<Scheduler::SystemId> Find(std::string_view name) const
    {
        const auto found = std::ranges::find(*m_systemNames, name);
        if (found == m_systemNames->end())
        {
            return std::nullopt;
        }
        return static_cast<Scheduler::SystemId>(found - m_systemNames->begin());
    }

    void Fail(std::string message)
    {
        if (m_error.empty())
        {
            m_error = std::move(message);
        }
    }

    const sdk::ModuleScope* m_scope;
    const values::DataRegistry* m_data;
    chain::ChainRegistry* m_chains;
    std::span<const std::byte> m_savedState;
    std::vector<std::string>* m_systemNames;
    Scheduler* m_scheduler;
    std::vector<std::pair<ComponentTypeId, ComponentMigrator>>* m_components;
    StateSaver* m_saver;
    std::vector<std::pair<std::string, std::string>> m_orders;
    std::span<const std::pair<std::string, values::Value>> m_savedFields;
    void** m_stateObject;
    std::vector<values::FieldInfo>* m_stateFields;
    std::string m_error;
};

/// One component value converted to a new layout, waiting to be applied.
struct Migration
{
    Entity entity;
    ComponentTypeId from;
    ComponentTypeId to;
    std::size_t offset; ///< Into the shared byte buffer.
};

/// Storage for migrated values, aligned for any component (alignas(64) covers cache-line padded types).
struct alignas(64) AlignedBlock
{
    std::byte bytes[64];
};

/// Converts every value of a module-owned component whose layout changed. Nothing in the world changes unless
/// every value converts.
Result<std::uint32_t> MigrateComponents(World& world,
                                        std::span<const std::pair<ComponentTypeId, ComponentMigrator>> components)
{
    std::vector<Migration> migrations;
    std::vector<AlignedBlock> buffer;
    std::size_t used = 0;
    for (const auto& [to, migrator] : components)
    {
        const ComponentInfo& toInfo = ComponentInfoOf(to);
        INTERI_VERIFY(toInfo.alignment <= alignof(AlignedBlock), "component alignment above 64");
        for (const auto& archetype : world.Archetypes())
        {
            for (const interi::detail::Column& column : archetype->columns)
            {
                const ComponentInfo& fromInfo = *column.info;
                if (column.type == to || fromInfo.name != toInfo.name)
                {
                    continue;
                }
                const bool tags = fromInfo.size == 0 && toInfo.size == 0;
                if (!tags && migrator == nullptr && !archetype->chunks.empty())
                {
                    return MakeError(ErrorCode::VersionMismatch,
                                     std::format("component '{}' changed layout (version {} -> {}) and the module "
                                                 "declares no migrator",
                                                 toInfo.name, fromInfo.version, toInfo.version));
                }
                for (const interi::detail::Chunk& chunk : archetype->chunks)
                {
                    for (std::uint32_t row = 0; row < chunk.count; ++row)
                    {
                        const Entity entity = archetype->Entities(chunk)[row];
                        const std::size_t offset =
                            (used + alignof(AlignedBlock) - 1) / alignof(AlignedBlock) * alignof(AlignedBlock);
                        used = offset + toInfo.size;
                        buffer.resize((used + sizeof(AlignedBlock) - 1) / sizeof(AlignedBlock));
                        if (!tags)
                        {
                            const void* value = archetype->At(
                                chunk, static_cast<std::size_t>(&column - archetype->columns.data()), row);
                            auto* destination = reinterpret_cast<std::byte*>(buffer.data()) + offset;
                            if (!migrator(fromInfo.version, value, fromInfo.size, destination))
                            {
                                return MakeError(ErrorCode::VersionMismatch,
                                                 std::format("the migrator of '{}' refused version {}", toInfo.name,
                                                             fromInfo.version));
                            }
                        }
                        migrations.push_back(
                            Migration{.entity = entity, .from = column.type, .to = to, .offset = offset});
                    }
                }
            }
        }
    }
    for (const Migration& migration : migrations)
    {
        void* data = ComponentInfoOf(migration.to).size == 0
                         ? nullptr
                         : reinterpret_cast<std::byte*>(buffer.data()) + migration.offset;
        (void)world.AddErased(migration.entity, ErasedComponent{.type = migration.to, .data = data});
        (void)world.RemoveErased(migration.entity, migration.from);
    }
    return static_cast<std::uint32_t>(migrations.size());
}

void LogContentWarnings(const values::ModuleContent& content)
{
    for (const std::string& warning : content.warnings)
    {
        INTERI_LOG_WARN("HotReload", "module '{}': {}", content.module, warning);
    }
}

std::vector<std::filesystem::path> LibraryFileNames(std::string_view base)
{
#if defined(_WIN32)
    return {std::format("{}.dll", base), std::format("lib{}.dll", base)};
#elif defined(__APPLE__)
    return {std::format("lib{}.dylib", base), std::format("{}.dylib", base), std::format("lib{}.so", base),
            std::format("{}.so", base)};
#else
    return {std::format("lib{}.so", base), std::format("{}.so", base)};
#endif
}

} // namespace

void ModuleHost::LogUnload(const Loaded& module)
{
    if (module.attached)
    {
        INTERI_LOG_INFO("HotReload", "unloading module '{}'", module.id);
    }
}

/// One transaction: modules leaving and the instances replacing them (both in load order). Nothing changes unless
/// every incoming module registers and every component migrates.
struct ModuleHost::Plan
{
    std::vector<std::string> outgoing;
    std::vector<std::unique_ptr<Loaded>> incoming;
    std::map<std::string, std::vector<std::byte>, std::less<>> savedStates;
    std::map<std::string, std::vector<std::pair<std::string, values::Value>>, std::less<>> savedFields;
    std::optional<sdk::ModuleSet> moduleSet; ///< Replaces the providers (a new project).
    std::optional<sdk::ModuleGraph> graph;
};

ModuleHost::ModuleHost(World& world, ModuleHostDesc desc)
    : m_world(&world)
    , m_desc(std::move(desc))
    , m_moduleSet(std::make_unique<sdk::ModuleSet>())
    , m_hooks(std::make_unique<sdk::HookRegistry>(*m_moduleSet))
    , m_data(std::make_unique<values::DataRegistry>(*m_moduleSet))
    , m_chains(std::make_unique<chain::ChainRegistry>(*m_moduleSet))
{
    for (std::string& id : chain::CoreContracts())
    {
        m_desc.engineContracts.push_back(std::move(id));
    }
    for (const std::string& id : m_desc.engineContracts)
    {
        m_moduleSet->DeclareContract(id);
    }
    auto core = chain::RegisterCoreLibrary(*m_chains);
    INTERI_VERIFY(core.has_value(), "the chain core library must register");
    if (m_desc.shadowDirectory.empty())
    {
        std::error_code ignored;
        m_desc.shadowDirectory = std::filesystem::temp_directory_path(ignored) / "interi_modules";
    }
}

ModuleHost::~ModuleHost()
{
    // As Unload, without its log line (formatting may throw; a destructor must not).
    while (!m_modules.empty())
    {
        m_hooks->Registry().RemoveProvider(m_modules.back()->id);
        m_data->RemoveProvider(m_modules.back()->id);
        m_chains->RemoveProvider(m_modules.back()->id);
        m_modules.pop_back();
    }
}

bool ModuleHost::Supported() noexcept
{
#if defined(INTERI_SHARED_ENGINE)
    return true;
#else
    return false;
#endif
}

Result<std::unique_ptr<ModuleHost::Loaded>> ModuleHost::Open(const std::string& id,
                                                             const std::filesystem::path& library)
{
    auto module = std::make_unique<Loaded>();
    module->id = id;
    module->scope = std::make_unique<sdk::ModuleScope>(*m_hooks, id);
    if (library.empty())
    {
        return module; // content only
    }
    module->libraryPath = library;

    std::error_code error;
    if (!std::filesystem::is_regular_file(library, error))
    {
        return MakeError(ErrorCode::NotFound, std::format("no module library at '{}'", library.string()));
    }
    std::filesystem::create_directories(m_desc.shadowDirectory, error);
    // A unique name per load: the OS would hand back the already loaded library for a path it has open.
    const std::filesystem::path shadow =
        m_desc.shadowDirectory /
        std::format("{}.{}{}", library.stem().string(), m_shadowCounter++, library.extension().string());
    if (!std::filesystem::copy_file(library, shadow, std::filesystem::copy_options::overwrite_existing, error))
    {
        return MakeError(ErrorCode::IoError,
                         std::format("copying '{}' to '{}': {}", library.string(), shadow.string(), error.message()));
    }
    module->shadowPath = shadow;
    INTERI_TRY_ASSIGN(opened, DynamicLibrary::Open(shadow.string()));
    module->library = std::move(opened);

    using GetApi = const ModuleApi* (*)();
    const auto getApi = module->library.FindFunction<GetApi>(kModuleApiSymbol);
    if (getApi == nullptr)
    {
        return MakeError(ErrorCode::NotFound,
                         std::format("'{}' does not export {}", library.string(), kModuleApiSymbol));
    }
    const ModuleApi* api = getApi();
    if (api == nullptr || api->abiVersion != kModuleAbiVersion || api->load == nullptr || api->unload == nullptr)
    {
        return MakeError(ErrorCode::VersionMismatch,
                         std::format("'{}' was built for module ABI {}, the host is {}", library.string(),
                                     api != nullptr ? api->abiVersion : 0, kModuleAbiVersion));
    }
    const std::string_view engine = sdk::EngineToolchainFingerprint();
    if (api->toolchain == nullptr || engine != api->toolchain)
    {
        return MakeError(ErrorCode::VersionMismatch,
                         std::format("'{}' was built with toolchain '{}', the engine with '{}'; rebuild it against "
                                     "this engine",
                                     library.string(), api->toolchain != nullptr ? api->toolchain : "", engine));
    }
    module->api = api;
    return module;
}

Result<void> ModuleHost::Register(Loaded& module, std::span<const std::byte> savedState,
                                  std::span<const std::pair<std::string, values::Value>> savedFields)
{
    if (module.api == nullptr)
    {
        return {};
    }
    Registrar registrar(*module.scope, *m_data, *m_chains, savedState, module.systemNames, *module.scheduler,
                        module.components, module.saver, savedFields, module.stateObject, module.stateFields);
    const ModuleContext context{.abiVersion = kModuleAbiVersion, .registrar = &registrar};
    module.attached = true;
    if (!module.api->load(context))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("module '{}' refused to load", module.id));
    }
    return registrar.Finish();
}

Result<std::filesystem::path> ModuleHost::FindLibrary(const sdk::ModuleManifest& manifest) const
{
    if (manifest.library.empty())
    {
        return std::filesystem::path{};
    }
    std::vector<std::filesystem::path> directories;
    if (!manifest.directory.empty())
    {
        directories.push_back(manifest.directory);
    }
    directories.insert(directories.end(), m_desc.librarySearchPaths.begin(), m_desc.librarySearchPaths.end());
    for (const std::filesystem::path& directory : directories)
    {
        for (const std::filesystem::path& name : LibraryFileNames(manifest.library))
        {
            std::error_code error;
            if (std::filesystem::is_regular_file(directory / name, error))
            {
                return directory / name;
            }
        }
    }
    return MakeError(ErrorCode::NotFound,
                     std::format("module '{}': library '{}' was not found", manifest.id, manifest.library));
}

Result<ModuleLoadStats> ModuleHost::Execute(Plan plan)
{
    // Take the outgoing modules' hooks out (kept for a rollback), switch providers if this is a new project, then
    // register the incoming modules in load order.
    std::vector<HookEntries> extracted;
    std::vector<values::ExtractedData> extractedData;
    std::vector<chain::ChainRegistry::Extracted> extractedChains;
    extracted.reserve(plan.outgoing.size());
    for (const std::string& id : plan.outgoing)
    {
        extracted.push_back(m_hooks->Registry().ExtractProvider(id));
        extractedData.push_back(m_data->ExtractProvider(id));
        extractedChains.push_back(m_chains->ExtractProvider(id));
    }
    std::optional<sdk::ModuleSet> previousSet;
    if (plan.moduleSet)
    {
        previousSet = std::move(*m_moduleSet);
        *m_moduleSet = std::move(*plan.moduleSet);
    }

    const auto rollback = [&](Error error) -> Result<ModuleLoadStats>
    {
        for (const auto& module : plan.incoming)
        {
            m_hooks->Registry().RemoveProvider(module->id);
            m_data->RemoveProvider(module->id);
            m_chains->RemoveProvider(module->id);
        }
        if (previousSet)
        {
            *m_moduleSet = std::move(*previousSet);
        }
        for (HookEntries& entries : extracted)
        {
            m_hooks->Registry().Restore(std::move(entries));
        }
        for (values::ExtractedData& data : extractedData)
        {
            m_data->Restore(std::move(data));
        }
        for (chain::ChainRegistry::Extracted& chains : extractedChains)
        {
            m_chains->Restore(std::move(chains));
        }
        // Dependents first, like any unload.
        while (!plan.incoming.empty())
        {
            plan.incoming.pop_back();
        }
        return std::unexpected(std::move(error));
    };

    std::vector<std::pair<ComponentTypeId, ComponentMigrator>> components;
    ModuleLoadStats stats;
    for (const auto& module : plan.incoming)
    {
        const auto saved = plan.savedStates.find(module->id);
        const std::span<const std::byte> state =
            saved != plan.savedStates.end() ? std::span<const std::byte>(saved->second) : std::span<const std::byte>{};
        if (auto added = m_data->AddModule(module->content); !added)
        {
            return rollback(std::move(added.error()));
        }
        const auto fields = plan.savedFields.find(module->id);
        const std::span<const std::pair<std::string, values::Value>> savedFields =
            fields != plan.savedFields.end() ? std::span<const std::pair<std::string, values::Value>>(fields->second)
                                             : std::span<const std::pair<std::string, values::Value>>{};
        if (auto registered = Register(*module, state, savedFields); !registered)
        {
            return rollback(std::move(registered.error()));
        }
        components.insert(components.end(), module->components.begin(), module->components.end());
        stats.systems += static_cast<std::uint32_t>(module->systemNames.size());
    }
    auto migrated = MigrateComponents(*m_world, components);
    if (!migrated)
    {
        return rollback(std::move(migrated.error()));
    }
    stats.migratedComponents = *migrated;
    stats.reloadedModules = static_cast<std::uint32_t>(plan.incoming.size());

    // Commit: the old hook entries go first (their code is still loaded), then the old instances, dependents first.
    // Character blocks follow their archetypes' new layouts; project data that no longer fits is reported.
    extracted.clear();
    extractedData.clear();
    extractedChains.clear();
    stats.migratedAttributeBlocks = m_data->Characters().MigrateBlocks(*m_world);
    for (const std::string& warning : m_data->Revalidate())
    {
        INTERI_LOG_WARN("HotReload", "{}", warning);
    }
    std::vector<std::unique_ptr<Loaded>> leaving;
    for (const std::string& id : plan.outgoing)
    {
        const auto it =
            std::ranges::find_if(m_modules, [&](const auto& module) { return module != nullptr && module->id == id; });
        if (it != m_modules.end())
        {
            leaving.push_back(std::move(*it));
            *it = nullptr;
        }
    }
    for (auto& module : leaving) // dependents first: `outgoing` is in that order
    {
        LogUnload(*module);
        module.reset();
    }

    if (plan.graph)
    {
        m_graph = std::move(*plan.graph);
        std::erase(m_modules, nullptr);
        for (auto& module : plan.incoming)
        {
            m_modules.push_back(std::move(module));
        }
    }
    else
    {
        // A reload keeps every module at its position in the load order.
        for (auto& module : plan.incoming)
        {
            const auto slot = std::ranges::find(m_modules, nullptr);
            INTERI_ASSERT(slot != m_modules.end(), "reload plan out of sync with the loaded modules");
            *slot = std::move(module);
        }
    }
    ++m_generation;
    return stats;
}

Result<ModuleLoadStats> ModuleHost::LoadProject(std::span<const std::string> projectOrder,
                                                std::span<const sdk::ModuleManifest> available)
{
    if (!Supported())
    {
        return MakeError(ErrorCode::Unsupported, "gameplay modules need the shared engine (INTERI_BUILD_SHARED)");
    }
    INTERI_TRY_ASSIGN(graph, sdk::ResolveModuleGraph(projectOrder, available, sdk::CurrentHost()));

    Plan plan;
    for (const auto& module : m_modules)
    {
        plan.outgoing.push_back(module->id);
        if (module->saver)
        {
            plan.savedStates[module->id] = module->saver();
        }
        plan.savedFields[module->id] = module->ReadState();
    }
    for (const std::size_t index : graph.loadOrder)
    {
        const sdk::ModuleManifest& manifest = graph.modules[index].manifest;
        INTERI_TRY_ASSIGN(library, FindLibrary(manifest));
        INTERI_TRY_ASSIGN(module, Open(manifest.id, library));
        INTERI_TRY_ASSIGN(content, values::LoadModuleContent(manifest));
        LogContentWarnings(content);
        module->content = std::move(content);
        plan.incoming.push_back(std::move(module));
    }
    // The outgoing modules may be the same ids; their order matters for the saved states only.
    std::ranges::reverse(plan.outgoing); // the old project unloads dependents first
    plan.moduleSet = sdk::ModuleSet(graph, m_desc.engineContracts);
    plan.graph = std::move(graph);
    INTERI_TRY_ASSIGN(stats, Execute(std::move(plan)));
    INTERI_LOG_INFO("HotReload", "loaded a project of {} modules (generation {}, {} systems)", m_modules.size(),
                    m_generation, stats.systems);
    return stats;
}

Result<ModuleLoadStats> ModuleHost::Load(const std::filesystem::path& path)
{
    if (!Supported())
    {
        return MakeError(ErrorCode::Unsupported, "gameplay modules need the shared engine (INTERI_BUILD_SHARED)");
    }
    const bool reload = m_modules.size() == 1 && m_modules.front()->id == kLocalModuleId;
    if (!m_modules.empty() && !reload)
    {
        return MakeError(ErrorCode::InvalidArgument, "a project is loaded; Load(path) only loads a single module");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return MakeError(ErrorCode::NotFound, std::format("no module at '{}'", path.string()));
    }

    Plan plan;
    if (reload)
    {
        plan.outgoing.emplace_back(kLocalModuleId);
        if (m_modules.front()->saver)
        {
            plan.savedStates[std::string(kLocalModuleId)] = m_modules.front()->saver();
        }
        plan.savedFields[std::string(kLocalModuleId)] = m_modules.front()->ReadState();
    }
    else
    {
        sdk::ModuleManifest manifest;
        manifest.id = std::string(kLocalModuleId);
        manifest.name = path.stem().string();
        sdk::ModuleGraph graph;
        graph.modules.push_back({.manifest = std::move(manifest), .implicit = false, .priority = 0});
        graph.loadOrder.push_back(0);
        plan.moduleSet = sdk::ModuleSet(graph, m_desc.engineContracts);
        plan.graph = std::move(graph);
    }
    INTERI_TRY_ASSIGN(module, Open(std::string(kLocalModuleId), path));
    plan.incoming.push_back(std::move(module));
    INTERI_TRY_ASSIGN(stats, Execute(std::move(plan)));
    INTERI_LOG_INFO("HotReload",
                    "loaded gameplay module '{}' (generation {}, {} systems, {} components migrated, {} component ids "
                    "in use)",
                    path.string(), m_generation, stats.systems, stats.migratedComponents, ComponentTypeCount());
    return stats;
}

Result<ModuleLoadStats> ModuleHost::Reload(std::string_view id)
{
    const auto found = std::ranges::find_if(m_modules, [&](const auto& module) { return module->id == id; });
    if (found == m_modules.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("module '{}' is not loaded", id));
    }

    // The module and everything that (transitively) depends on it, in load order.
    std::vector<std::string> affected{std::string(id)};
    for (const auto& module : m_modules)
    {
        const sdk::ResolvedModule* resolved = m_graph.Get(module->id);
        if (resolved == nullptr || std::ranges::find(affected, module->id) != affected.end())
        {
            continue;
        }
        const bool dependent =
            std::ranges::any_of(resolved->manifest.dependencies, [&](const sdk::ModuleDependency& dependency)
                                { return std::ranges::find(affected, dependency.id) != affected.end(); });
        if (dependent)
        {
            affected.push_back(module->id);
        }
    }
    std::ranges::stable_sort(affected,
                             [&](const std::string& a, const std::string& b)
                             {
                                 const auto position = [&](const std::string& name)
                                 {
                                     return std::ranges::find_if(m_modules,
                                                                 [&](const auto& m) { return m->id == name; }) -
                                            m_modules.begin();
                                 };
                                 return position(a) < position(b);
                             });

    Plan plan;
    for (const std::string& name : affected)
    {
        const auto it = std::ranges::find_if(m_modules, [&](const auto& module) { return module->id == name; });
        INTERI_TRY_ASSIGN(module, Open(name, (*it)->libraryPath));
        if (const sdk::ResolvedModule* resolved = m_graph.Get(name))
        {
            INTERI_TRY_ASSIGN(content, values::LoadModuleContent(resolved->manifest));
            LogContentWarnings(content);
            module->content = std::move(content);
        }
        plan.incoming.push_back(std::move(module));
        if ((*it)->saver)
        {
            plan.savedStates[name] = (*it)->saver();
        }
        plan.savedFields[name] = (*it)->ReadState();
    }
    plan.outgoing.assign(affected.rbegin(), affected.rend()); // dependents leave first
    INTERI_TRY_ASSIGN(stats, Execute(std::move(plan)));
    INTERI_LOG_INFO("HotReload", "reloaded module '{}' and {} dependents (generation {})", id, affected.size() - 1,
                    m_generation);
    return stats;
}

void ModuleHost::Unload()
{
    // Dependents first; each module's hooks leave the registry before its code is unloaded.
    while (!m_modules.empty())
    {
        m_hooks->Registry().RemoveProvider(m_modules.back()->id);
        m_data->RemoveProvider(m_modules.back()->id);
        m_chains->RemoveProvider(m_modules.back()->id);
        LogUnload(*m_modules.back());
        m_modules.pop_back();
    }
    m_graph = {};
    *m_moduleSet = sdk::ModuleSet();
}

std::vector<std::string> ModuleHost::LoadedModules() const
{
    std::vector<std::string> ids;
    ids.reserve(m_modules.size());
    for (const auto& module : m_modules)
    {
        ids.push_back(module->id);
    }
    return ids;
}

std::vector<std::string> ModuleHost::SystemNames() const
{
    std::vector<std::string> names;
    for (const auto& module : m_modules)
    {
        names.insert(names.end(), module->systemNames.begin(), module->systemNames.end());
    }
    return names;
}

void ModuleHost::RunSystems(JobSystem& jobs, CommandBuffers& commands)
{
    for (const auto& module : m_modules)
    {
        if (module->api != nullptr) // content-only modules have no systems
        {
            module->scheduler->Run(*m_world, jobs, commands);
        }
    }
}

} // namespace interi::hotreload
