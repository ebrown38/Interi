// SPDX-License-Identifier: MIT
#pragma once

#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace interi
{

/// Upper bound on distinct component types in a process (archetype masks are bitsets of this size).
inline constexpr std::size_t kMaxComponentTypes = 256;

/// Process-wide component type index, assigned on first use of ComponentId<T>(). The value depends on first-use
/// order, so it must never influence storage layout, iteration order or hashed state.
///
/// Identity is the component's name: `T::kComponentName` when the type declares one (stable across compilers and
/// across hot-reloaded modules), otherwise the compiler's type name. Registering a name again with the same size,
/// alignment and version returns the existing id; a changed layout gets a new id under the same name (hot reload
/// migrates between them). Names in an anonymous namespace are never shared.
using ComponentTypeId = std::uint32_t;
using ComponentMask = std::bitset<kMaxComponentTypes>;

/// A type that can be stored in the ECS: rows are relocated with noexcept moves. Empty types are tags: they take no
/// storage and must be trivially destructible.
template <typename T>
concept Component = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
                    std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                    std::is_nothrow_destructible_v<T> && (!std::is_empty_v<T> || std::is_trivially_destructible_v<T>);

/// Runtime type information the ECS needs to store a component type without knowing it statically.
struct ComponentInfo
{
    std::string name;          ///< kComponentName or the compiler's type name; orders columns within a chunk.
    std::size_t size = 0;      ///< 0 for tags.
    std::size_t alignment = 1; ///< Of the type; tags report 1.
    /// Layout version (T::kComponentVersion, default 1), for migrating state across hot reloads.
    std::uint32_t version = 1;
    /// Move-constructs *destination from *source, then destroys *source. The three functions are null for trivially
    /// copyable types (copied with memcpy, nothing to destroy), so no registry entry of such a type points into the
    /// code of a hot-reloaded module. Call them through RelocateComponent / MoveAssignComponent / DestroyComponent.
    void (*relocate)(void* destination, void* source) noexcept = nullptr;
    void (*moveAssign)(void* destination, void* source) noexcept = nullptr;
    void (*destroy)(void* object) noexcept = nullptr;
};

inline void RelocateComponent(const ComponentInfo& info, void* destination, void* source) noexcept
{
    if (info.relocate != nullptr)
    {
        info.relocate(destination, source);
    }
    else
    {
        std::memcpy(destination, source, info.size);
    }
}

inline void MoveAssignComponent(const ComponentInfo& info, void* destination, void* source) noexcept
{
    if (info.moveAssign != nullptr)
    {
        info.moveAssign(destination, source);
    }
    else
    {
        std::memcpy(destination, source, info.size);
    }
}

inline void DestroyComponent(const ComponentInfo& info, void* object) noexcept
{
    if (info.destroy != nullptr)
    {
        info.destroy(object);
    }
}

/// A component value of a type known only at runtime: `data` points to a live object of `type` (nullptr for tags).
struct ErasedComponent
{
    ComponentTypeId type;
    void* data;
};

/// Registers a component type and returns its id, or the existing id of the same name and layout (see
/// ComponentTypeId). Thread-safe. Asserts when kMaxComponentTypes is exceeded. Prefer ComponentId<T>().
[[nodiscard]] ComponentTypeId RegisterComponent(ComponentInfo info);

/// Info for an id returned by RegisterComponent. Thread-safe.
[[nodiscard]] const ComponentInfo& ComponentInfoOf(ComponentTypeId id) noexcept;

/// Number of registered ids (ids are 0 .. count-1, never removed). Thread-safe.
[[nodiscard]] std::uint32_t ComponentTypeCount() noexcept;

/// True for compiler type names inside an anonymous namespace (Clang/GCC and MSVC spellings).
[[nodiscard]] bool IsAnonymousComponentName(std::string_view name) noexcept;

/// The component registry. Every binary that links interi_ecs has one; a hot-reloaded module attaches to the host's
/// so that both see the same ids.
class ComponentRegistry;

/// The registry this binary registers into: its own, or the host's after AttachToHostComponentRegistry.
[[nodiscard]] ComponentRegistry& ActiveComponentRegistry() noexcept;

/// For a hot-reloaded module's copy of interi_ecs: registers into `host` from now on, in module mode. A module may
/// share the host's existing types (their functions live in the host) and register new trivially copyable ones; a
/// new non-trivial type asserts, because its functions would live in the module and dangle once it is unloaded.
void AttachToHostComponentRegistry(ComponentRegistry& host) noexcept;

/// Undoes AttachToHostComponentRegistry (back to this binary's own registry, not in module mode).
void DetachFromHostComponentRegistry() noexcept;

namespace detail
{

template <typename T>
[[nodiscard]] constexpr std::string_view RawTypeName() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

/// The type's name as the compiler spells it, e.g. "game::Position" (MSVC: "struct game::Position").
template <typename T>
[[nodiscard]] constexpr std::string_view TypeName() noexcept
{
    constexpr std::string_view raw = RawTypeName<T>();
#if defined(_MSC_VER) && !defined(__clang__)
    constexpr std::string_view open = "RawTypeName<";
    constexpr std::string_view close = ">(void)";
#else
    constexpr std::string_view open = "T = ";
    constexpr std::string_view close = "]";
#endif
    constexpr std::size_t begin = raw.find(open) + open.size();
    constexpr std::size_t end = raw.rfind(close);
    return raw.substr(begin, end - begin);
}

template <typename T>
concept HasComponentName = requires {
    { T::kComponentName } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept HasComponentVersion = requires {
    { T::kComponentVersion } -> std::convertible_to<std::uint32_t>;
};

template <Component T>
[[nodiscard]] ComponentInfo MakeComponentInfo()
{
    ComponentInfo info;
    if constexpr (HasComponentName<T>)
    {
        info.name = std::string(std::string_view(T::kComponentName));
    }
    else
    {
        info.name = std::string(TypeName<T>());
    }
    if constexpr (HasComponentVersion<T>)
    {
        info.version = static_cast<std::uint32_t>(T::kComponentVersion);
    }
    if constexpr (!std::is_empty_v<T>)
    {
        info.size = sizeof(T);
        info.alignment = alignof(T);
    }
    if constexpr (!std::is_empty_v<T> && !std::is_trivially_copyable_v<T>)
    {
        info.relocate = [](void* destination, void* source) noexcept
        {
            T* from = std::launder(static_cast<T*>(source));
            ::new (destination) T(std::move(*from));
            std::destroy_at(from);
        };
        info.moveAssign = [](void* destination, void* source) noexcept
        { *std::launder(static_cast<T*>(destination)) = std::move(*std::launder(static_cast<T*>(source))); };
        info.destroy = [](void* object) noexcept { std::destroy_at(std::launder(static_cast<T*>(object))); };
    }
    return info;
}

} // namespace detail

/// The id of component type T, registering it on first use. Thread-safe.
template <Component T>
[[nodiscard]] ComponentTypeId ComponentId()
{
    static const ComponentTypeId id = RegisterComponent(detail::MakeComponentInfo<T>());
    return id;
}

} // namespace interi
