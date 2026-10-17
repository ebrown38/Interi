// SPDX-License-Identifier: MIT
#pragma once

// The boundary between the engine (host) and a hot-reloadable gameplay module (a .so / .dll built from game code).
//
// A module statically links its own copy of interi_core, interi_jobs and interi_ecs. Everything that copy reaches
// globally is redirected to the host while the module is loaded: the component registry (so both see the same
// component ids), the global logger and the assert handler. The JobSystem and World are host objects (JobSystem
// keeps its thread state in host code). The rules that keep this safe:
//   - Components a module adds are trivially copyable and have an explicit kComponentName (DeclareComponent checks
//     both), so nothing in the registry points into module code, and the module's types find the host's ids again
//     after a reload. Bump kComponentVersion when the layout changes and pass a migrator.
//   - Module code must not give the host anything that outlives the module: allocations from the module's own
//     default allocator, function pointers or objects with module vtables. Systems are destroyed before the module
//     is unloaded.
//   - Modules never touch Jolt (their copy of it is not initialised): they read and write ECS components, and
//     engine systems do the physics.
//
// Write a module like this:
//
//   bool Register(interi::hotreload::ModuleRegistrar& registrar)
//   {
//       interi::hotreload::DeclareComponent<Health>(registrar);
//       registrar.AddSystem("Regenerate", interi::SystemAccess{}.Write<Health>(), [](interi::SystemContext& c) {...});
//       return true;
//   }
//   INTERI_GAMEPLAY_MODULE(Register)

#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace interi::hotreload
{

/// Bumped whenever ModuleContext, ModuleApi or ModuleRegistrar change.
inline constexpr std::uint32_t kModuleAbiVersion = 1;

/// Converts one component from an older layout version to the declared one: reads `fromSize` bytes at `from`
/// (written by a module with `fromVersion`) and writes a complete new value to `to`. False if it can't.
using ComponentMigrator = bool (*)(std::uint32_t fromVersion, const void* from, std::size_t fromSize, void* to);

/// What a module registers into, implemented by the host.
class ModuleRegistrar
{
public:
    ModuleRegistrar() = default;
    ModuleRegistrar(const ModuleRegistrar&) = delete;
    ModuleRegistrar& operator=(const ModuleRegistrar&) = delete;
    ModuleRegistrar(ModuleRegistrar&&) = delete;
    ModuleRegistrar& operator=(ModuleRegistrar&&) = delete;

    /// Declares a component type the module owns (use DeclareComponent<T>). On reload, entities holding the same
    /// name with another layout are migrated with `migrator` (nullptr: such a reload is rejected).
    virtual void DeclareComponent(ComponentTypeId type, ComponentMigrator migrator) = 0;
    /// Adds a system to the module's scheduler. Names must be unique within the module.
    virtual void AddSystem(std::string_view name, SystemAccess access, Scheduler::SystemFunction function) = 0;
    /// `system` runs after `dependency` (both by name).
    virtual void RunAfter(std::string_view system, std::string_view dependency) = 0;

protected:
    ~ModuleRegistrar() = default;
};

/// Declares module-owned component T.
template <Component T>
void DeclareComponent(ModuleRegistrar& registrar, ComponentMigrator migrator = nullptr)
{
    static_assert(std::is_trivially_copyable_v<T>, "hot-reloadable components must be trivially copyable");
    static_assert(detail::HasComponentName<T>, "hot-reloadable components need a kComponentName");
    registrar.DeclareComponent(ComponentId<T>(), migrator);
}

/// Handed to the module when it is loaded.
struct ModuleContext
{
    std::uint32_t abiVersion = kModuleAbiVersion;
    ComponentRegistry* registry = nullptr;
    Logger* logger = nullptr;
    AssertHandler assertHandler = nullptr;
    ModuleRegistrar* registrar = nullptr;
};

/// The module's exported entry points.
struct ModuleApi
{
    std::uint32_t abiVersion = kModuleAbiVersion;
    /// Attaches the module to the host (registry, logger, assert handler) and registers its components and
    /// systems. False rejects the module.
    bool (*load)(const ModuleContext& context) = nullptr;
    /// Called before the module is closed (after its systems were destroyed).
    void (*unload)() = nullptr;
};

/// Name of the exported `const ModuleApi* InteriGetModuleApi()`.
inline constexpr const char* kModuleApiSymbol = "InteriGetModuleApi";

namespace detail
{

/// True while this module's own (static) copy of the engine is redirected to the host. A module linked against the
/// shared engine uses the host's globals directly and must not touch them.
inline bool& AttachedToHost() noexcept
{
    static bool attached = false;
    return attached;
}

/// The module side of ModuleApi::load: redirect this copy's globals to the host (static engine only), then register.
template <bool (*Register)(ModuleRegistrar&)>
bool LoadModule(const ModuleContext& context)
{
    if (context.abiVersion != kModuleAbiVersion || context.registry == nullptr || context.registrar == nullptr)
    {
        return false;
    }
    AttachedToHost() = &ActiveComponentRegistry() != context.registry;
    if (AttachedToHost())
    {
        AttachToHostComponentRegistry(*context.registry);
        (void)SetGlobalLogger(context.logger);
        (void)SetAssertHandler(context.assertHandler);
    }
    return Register(*context.registrar);
}

inline void UnloadModule()
{
    if (!AttachedToHost())
    {
        return;
    }
    AttachedToHost() = false;
    (void)SetGlobalLogger(nullptr);
    (void)SetAssertHandler(nullptr);
    DetachFromHostComponentRegistry();
}

} // namespace detail

} // namespace interi::hotreload

#if defined(_WIN32)
#define INTERI_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define INTERI_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Defines the module's entry point; `registerFunction` is a `bool (ModuleRegistrar&)`.
// A function name passed as a template argument can't be parenthesised.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define INTERI_GAMEPLAY_MODULE(registerFunction)                                                                       \
    INTERI_MODULE_EXPORT const ::interi::hotreload::ModuleApi* InteriGetModuleApi()                                    \
    {                                                                                                                  \
        static const ::interi::hotreload::ModuleApi api{                                                               \
            .abiVersion = ::interi::hotreload::kModuleAbiVersion,                                                      \
            .load = &::interi::hotreload::detail::LoadModule<&registerFunction>,                                       \
            .unload = &::interi::hotreload::detail::UnloadModule,                                                      \
        };                                                                                                             \
        return &api;                                                                                                   \
    }
// NOLINTEND(bugprone-macro-parentheses)
