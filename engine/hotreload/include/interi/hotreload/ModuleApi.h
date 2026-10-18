// SPDX-License-Identifier: MIT
#pragma once

// The boundary between the engine (host) and a gameplay module: a Module, Extension or Plugin built as a shared
// library (.so / .dll) from game code, described by a module.json manifest (see interi/sdk/Manifest.h).
//
// ABI v4 (v2 made the engine shared; v3 added module data; v4 Chain Reaction events): the engine is a set of shared
// libraries (INTERI_BUILD_SHARED) and every module links them, so the host and all modules share one component
// registry, logger, assert handler and job thread state. Modules are distributed as source and built against the
// engine; the loader rejects a module whose toolchain fingerprint (compiler, standard library, debug level, ABI
// version, build switches; see interi/sdk/Toolchain.h) differs from the engine's, which is what makes C++ types (std
// containers, std::function, vtables) safe across the boundary. The rules that remain:
//   - Components a module owns are trivially copyable and have an explicit kComponentName (DeclareComponent checks
//     both): the World keeps them across a reload with no pointers into module code, and the reloaded module finds
//     the same ids. Bump kComponentVersion when the layout changes and pass a migrator.
//   - Module code must not leave the host anything that outlives the module: objects with module vtables, function
//     pointers, std::functions. Systems, hooks and the state saver are destroyed before the module is unloaded.
//   - Dependents reach an Extension through hooks and registry items resolved at run time, not by linking against
//     its library (a shadow-copied DLL wouldn't satisfy a link-time import on Windows).
// Module data (Values, attributes, data tables) is declared in module.json, not in code; the host registers it
// before the module's Register runs, and module code reads it through Data() (owned by the host, so a system may
// keep the reference).
//
// Write a module like this:
//
//   bool Register(interi::hotreload::ModuleRegistrar& registrar)
//   {
//       interi::hotreload::DeclareComponent<Health>(registrar);
//       registrar.AddSystem("Regenerate", interi::SystemAccess{}.Write<Health>(), [](interi::SystemContext& c) {...});
//       interi::hotreload::DeclareState(registrar, g_state, {interi::values::Field("kills", &State::kills)});
//       const auto& scope = registrar.Scope(); // hooks: DeclareHook, DeclareFactory, INTERI_OVERRIDE
//       registrar.Chains().Events().Provide(scope.Provider(), scope.Local("FireGun"), ...); // chain events
//       return scope.DeclareHook<int(int)>(scope.Local("Damage"), ...).has_value();
//   }
//   INTERI_GAMEPLAY_MODULE(Register)

#include <interi/chain/Event.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Toolchain.h>
#include <interi/values/DataRegistry.h>
#include <interi/values/Reflection.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace interi::hotreload
{

/// Bumped whenever ModuleContext, ModuleApi, ModuleRegistrar, the hook types, DataRegistry or the chain registry
/// change (part of the fingerprint).
inline constexpr std::uint32_t kModuleAbiVersion = INTERI_MODULE_ABI_VERSION;

/// Converts one component from an older layout version to the declared one: reads `fromSize` bytes at `from`
/// (written by a module with `fromVersion`) and writes a complete new value to `to`. False if it can't.
using ComponentMigrator = bool (*)(std::uint32_t fromVersion, const void* from, std::size_t fromSize, void* to);

/// Saves module-global state before the module is reloaded; the bytes reach the next instance (SavedState).
using StateSaver = std::function<std::vector<std::byte>()>;

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

    /// The module's provider id and the project's hooks (declare hooks and factories, override other modules').
    [[nodiscard]] virtual const sdk::ModuleScope& Scope() const = 0;
    /// The project's data: every loaded module's Values, attributes and tables with the project's overrides. Valid
    /// until the host is destroyed.
    [[nodiscard]] virtual const values::DataRegistry& Data() const = 0;
    /// The project's Chain Reaction events and functions: provide the module's own (its provider id is
    /// Scope().Provider()), or override a lower module's. Implementations must stay pure and thread-safe (see
    /// interi/chain/Event.h); the host removes them before the module is unloaded, and chain executors recompile.
    [[nodiscard]] virtual chain::ChainRegistry& Chains() const = 0;
    /// Declares module-global state as reflected fields of the object at `object` (use DeclareState). The host
    /// writes the previous instance's values of fields with the same name and type into it now, and reads them
    /// again before this instance is unloaded for a reload; fields that are new or changed type keep what the module
    /// initialised them to. `object` must stay valid while the module is loaded (a global).
    virtual void DeclareState(void* object, std::vector<values::FieldInfo> fields) = 0;
    /// What the previous instance's StateSaver returned (empty on the first load).
    [[nodiscard]] virtual std::span<const std::byte> SavedState() const = 0;
    /// Called before this instance is unloaded for a reload.
    virtual void SetStateSaver(StateSaver saver) = 0;

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

/// Declares `state` (a module global) as the module's reflected state: its `fields` (values::Field(&T::member))
/// survive reloads by name.
template <typename T>
void DeclareState(ModuleRegistrar& registrar, T& state, std::vector<values::FieldInfo> fields)
{
    registrar.DeclareState(&state, std::move(fields));
}

/// Handed to the module when it is loaded.
struct ModuleContext
{
    std::uint32_t abiVersion = kModuleAbiVersion;
    ModuleRegistrar* registrar = nullptr;
};

/// The module's exported entry points.
struct ModuleApi
{
    std::uint32_t abiVersion = kModuleAbiVersion;
    /// INTERI_TOOLCHAIN_FINGERPRINT of the module binary; must equal the engine's.
    const char* toolchain = nullptr;
    /// Registers the module's components, systems and hooks. False rejects the module.
    bool (*load)(const ModuleContext& context) = nullptr;
    /// Called before the module is closed (after its systems and hooks were destroyed).
    void (*unload)() = nullptr;
};

/// Name of the exported `const ModuleApi* InteriGetModuleApi()`.
inline constexpr const char* kModuleApiSymbol = "InteriGetModuleApi";

namespace detail
{

/// The module side of ModuleApi::load.
template <bool (*Register)(ModuleRegistrar&)>
bool LoadModule(const ModuleContext& context)
{
    if (context.abiVersion != kModuleAbiVersion || context.registrar == nullptr)
    {
        return false;
    }
    return Register(*context.registrar);
}

inline void UnloadModule() {}

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
            .toolchain = INTERI_TOOLCHAIN_FINGERPRINT,                                                                 \
            .load = &::interi::hotreload::detail::LoadModule<&registerFunction>,                                       \
            .unload = &::interi::hotreload::detail::UnloadModule,                                                      \
        };                                                                                                             \
        return &api;                                                                                                   \
    }
// NOLINTEND(bugprone-macro-parentheses)
