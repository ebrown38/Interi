// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/platform/DynamicLibrary.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>

namespace interi::hotreload
{

struct ModuleHost::Loaded
{
    DynamicLibrary library;
    std::filesystem::path shadowPath;
    const ModuleApi* api = nullptr;
    bool attached = false; ///< api->load was called, so api->unload must be.
    std::unique_ptr<Scheduler> scheduler = std::make_unique<Scheduler>();
    std::vector<std::string> systemNames; ///< By Scheduler::SystemId.
    std::vector<std::pair<ComponentTypeId, ComponentMigrator>> components;

    Loaded() = default;
    Loaded(const Loaded&) = delete;
    Loaded& operator=(const Loaded&) = delete;
    Loaded(Loaded&&) = delete;
    Loaded& operator=(Loaded&&) = delete;

    ~Loaded()
    {
        // The systems (and whatever they captured) are module code: destroy them while the module is still loaded.
        scheduler.reset();
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
    explicit Registrar(std::vector<std::string>& systemNames, Scheduler& scheduler,
                       std::vector<std::pair<ComponentTypeId, ComponentMigrator>>& components)
        : m_systemNames(&systemNames)
        , m_scheduler(&scheduler)
        , m_components(&components)
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

    std::vector<std::string>* m_systemNames;
    Scheduler* m_scheduler;
    std::vector<std::pair<ComponentTypeId, ComponentMigrator>>* m_components;
    std::vector<std::pair<std::string, std::string>> m_orders;
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

} // namespace

ModuleHost::ModuleHost(World& world, ModuleHostDesc desc)
    : m_world(&world)
    , m_shadowDirectory(std::move(desc.shadowDirectory))
{
    if (m_shadowDirectory.empty())
    {
        std::error_code ignored;
        m_shadowDirectory = std::filesystem::temp_directory_path(ignored) / "interi_modules";
    }
}

ModuleHost::~ModuleHost() = default;

Result<ModuleLoadStats> ModuleHost::Load(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return MakeError(ErrorCode::NotFound, std::format("no module at '{}'", path.string()));
    }
    std::filesystem::create_directories(m_shadowDirectory, error);
    auto next = std::make_unique<Loaded>();
    // A unique name per load: the OS would hand back the already loaded library for a path it has open.
    const std::filesystem::path shadow =
        m_shadowDirectory / std::format("{}.{}{}", path.stem().string(), m_shadowCounter++, path.extension().string());
    if (!std::filesystem::copy_file(path, shadow, std::filesystem::copy_options::overwrite_existing, error))
    {
        return MakeError(ErrorCode::IoError,
                         std::format("copying '{}' to '{}': {}", path.string(), shadow.string(), error.message()));
    }
    next->shadowPath = shadow;
    INTERI_TRY_ASSIGN(library, DynamicLibrary::Open(shadow.string()));
    next->library = std::move(library);

    using GetApi = const ModuleApi* (*)();
    const auto getApi = next->library.FindFunction<GetApi>(kModuleApiSymbol);
    if (getApi == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("'{}' does not export {}", path.string(), kModuleApiSymbol));
    }
    const ModuleApi* api = getApi();
    if (api == nullptr || api->abiVersion != kModuleAbiVersion || api->load == nullptr || api->unload == nullptr)
    {
        return MakeError(ErrorCode::VersionMismatch,
                         std::format("'{}' was built for module ABI {}, the host is {}", path.string(),
                                     api != nullptr ? api->abiVersion : 0, kModuleAbiVersion));
    }
    next->api = api;

    Registrar registrar(next->systemNames, *next->scheduler, next->components);
    const ModuleContext context{
        .abiVersion = kModuleAbiVersion,
        .registry = &ActiveComponentRegistry(),
        .logger = GetGlobalLogger(),
        .assertHandler = GetAssertHandler(),
        .registrar = &registrar,
    };
    next->attached = true;
    if (!api->load(context))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("'{}' refused to load", path.string()));
    }
    INTERI_TRY(registrar.Finish());
    INTERI_TRY_ASSIGN(migrated, MigrateComponents(*m_world, next->components));

    const ModuleLoadStats stats{.migratedComponents = migrated,
                                .systems = static_cast<std::uint32_t>(next->systemNames.size())};
    m_current = std::move(next); // destroys the previous module, if any
    ++m_generation;
    INTERI_LOG_INFO("HotReload",
                    "loaded gameplay module '{}' (generation {}, {} systems, {} components migrated, {} component ids "
                    "in use)",
                    path.string(), m_generation, stats.systems, stats.migratedComponents, ComponentTypeCount());
    return stats;
}

void ModuleHost::Unload()
{
    m_current.reset();
}

std::vector<std::string> ModuleHost::SystemNames() const
{
    return m_current != nullptr ? m_current->systemNames : std::vector<std::string>{};
}

void ModuleHost::RunSystems(JobSystem& jobs, CommandBuffers& commands)
{
    if (m_current != nullptr)
    {
        m_current->scheduler->Run(*m_world, jobs, commands);
    }
}

} // namespace interi::hotreload
