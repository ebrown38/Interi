// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Scheduler.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/jobs/JobSystem.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace interi::hotreload
{

struct ModuleHostDesc
{
    /// Where loaded modules are copied before opening them (so a rebuild can overwrite the original, which Windows
    /// locks while it is loaded). Empty: a directory under the system temp directory.
    std::filesystem::path shadowDirectory;
};

/// Per-load counters.
struct ModuleLoadStats
{
    std::uint32_t migratedComponents = 0; ///< Component values converted to a new layout.
    std::uint32_t systems = 0;
};

/// Loads a gameplay module (see ModuleApi.h), runs its systems, and replaces it with a rebuilt one while the World
/// keeps its entities.
///
/// Load(path) also reloads: the new module is opened and registered next to the old one, and entities whose
/// module-owned components changed layout are migrated with the new module's migrators. Any failure (missing file
/// or entry point, ABI mismatch, registration refused, a layout change without a migrator, a migrator refusing) leaves
/// the old module and the World untouched. Only once everything succeeded are the old systems destroyed and the old
/// library closed.
///
/// The module's systems run in a Scheduler of their own (RunSystems). Re-registered systems start with no change
/// history, so Changed filters match everything on their first run after a reload. Not thread-safe; call between
/// frames.
class ModuleHost
{
public:
    explicit ModuleHost(World& world, ModuleHostDesc desc = {});
    ~ModuleHost();

    ModuleHost(const ModuleHost&) = delete;
    ModuleHost& operator=(const ModuleHost&) = delete;
    ModuleHost(ModuleHost&&) = delete;
    ModuleHost& operator=(ModuleHost&&) = delete;

    /// Loads (or reloads) the module at `path`.
    Result<ModuleLoadStats> Load(const std::filesystem::path& path);
    /// Destroys the module's systems and closes it.
    void Unload();

    [[nodiscard]] bool IsLoaded() const noexcept { return m_current != nullptr; }
    /// Successful loads so far.
    [[nodiscard]] std::uint32_t Generation() const noexcept { return m_generation; }
    /// The loaded module's system names, in registration order.
    [[nodiscard]] std::vector<std::string> SystemNames() const;

    /// Runs the module's systems once (then plays back `commands`). No-op without a module.
    void RunSystems(JobSystem& jobs, CommandBuffers& commands);

private:
    struct Loaded;

    World* m_world;
    std::filesystem::path m_shadowDirectory;
    std::unique_ptr<Loaded> m_current;
    std::uint32_t m_generation = 0;
    std::uint32_t m_shadowCounter = 0;
};

} // namespace interi::hotreload
