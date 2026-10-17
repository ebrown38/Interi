// SPDX-License-Identifier: MIT
#pragma once

// Hook points: named, typed override points a module exposes to Plugins and higher modules. Plugins override only
// what is exposed this way (hooks and class factories) or as registry items; they can't patch arbitrary code.
//
//   // The owner declares the hook and its default implementation (a local id, or a declared shared id):
//   hooks.Declare<int(int)>("com.author.fps", "com.author.fps:Damage", [](const HookNext<int(int)>&, int x)
//                           { return x; });
//
//   // A higher module or Plugin (manifest: "overrides": [{ "target": "com.author.fps:Damage", ... }]):
//   int DoubleDamage(const interi::sdk::HookNext<int(int)>& next, int x) { return 2 * next(x); }
//   INTERI_OVERRIDE(scope, "com.author.fps:Damage", DoubleDamage);
//
//   // Callers resolve a typed handle once and call it:
//   HookPoint<int(int)> damage = hooks.Find<int(int)>("com.author.fps:Damage");
//   int hp = damage(10);  // 20: the override runs and calls the default through `next`
//
// Each implementation receives `next`, the implementation it replaced (the next lower provider). A ClassFactory<T>
// is a hook returning std::unique_ptr<T>, so an override can wrap or replace what the lower factory creates.
// Not thread-safe to declare or override; calls may run concurrently while nothing is declared or overridden.

#include <interi/core/Assert.h>
#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>

#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace interi::sdk
{

template <typename Signature>
class HookNext;
template <typename Signature>
class HookPoint;

/// An implementation of a hook with signature R(Args...).
template <typename Signature>
struct HookImplTraits;
template <typename R, typename... Args>
struct HookImplTraits<R(Args...)>
{
    using Function = std::function<R(const HookNext<R(Args...)>&, Args...)>;
};
template <typename Signature>
using HookImpl = HookImplTraits<Signature>::Function;

namespace detail
{

/// A stable spelling of a signature type, so a hook's signature can be checked across binaries.
template <typename T>
[[nodiscard]] constexpr std::string_view SignatureName() noexcept
{
#if defined(_MSC_VER) && !defined(__clang__)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

/// One provider's implementation, type-erased.
struct ErasedHook
{
    std::string signature;
    std::shared_ptr<const void> implementation; ///< A HookImpl<Signature>.
};

/// The signature R(Args...) of a callable taking (const HookNext<R(Args...)>&, Args...).
template <typename F>
struct HookSignatureOf : HookSignatureOf<decltype(&std::remove_cvref_t<F>::operator())>
{
};
template <typename R, typename... Args>
struct HookSignatureOf<R (*)(const HookNext<R(Args...)>&, Args...)>
{
    using Type = R(Args...);
};
template <typename R, typename... Args>
struct HookSignatureOf<R(const HookNext<R(Args...)>&, Args...)>
{
    using Type = R(Args...);
};
template <typename C, typename R, typename... Args>
struct HookSignatureOf<R (C::*)(const HookNext<R(Args...)>&, Args...) const>
{
    using Type = R(Args...);
};
template <typename C, typename R, typename... Args>
struct HookSignatureOf<R (C::*)(const HookNext<R(Args...)>&, Args...)>
{
    using Type = R(Args...);
};

} // namespace detail

/// One provider's entry in a hook's stack.
using HookEntry = ContributionRegistry<detail::ErasedHook>::Entry;

/// Calls the implementation below the current one. Calling it at the bottom of the stack asserts (and returns a
/// value-initialised R); check HasNext() first when that can happen.
template <typename R, typename... Args>
class HookNext<R(Args...)>
{
public:
    HookNext(std::span<const HookEntry> stack, std::size_t index) noexcept
        : m_stack(stack)
        , m_index(index)
    {
    }

    [[nodiscard]] bool HasNext() const noexcept { return m_index < m_stack.size(); }

    R operator()(Args... args) const
    {
        if (!HasNext())
        {
            INTERI_ASSERT(false, "HookNext called below the lowest implementation");
            if constexpr (!std::is_void_v<R>)
            {
                return R{};
            }
            else
            {
                return;
            }
        }
        const auto& implementation =
            *static_cast<const HookImpl<R(Args...)>*>(m_stack[m_index].value.implementation.get());
        return implementation(HookNext(m_stack, m_index + 1), std::forward<Args>(args)...);
    }

private:
    std::span<const HookEntry> m_stack;
    std::size_t m_index;
};

/// The hooks of a project: one registry of type-erased implementations.
class HookRegistry
{
public:
    /// `modules` must outlive the registry.
    explicit HookRegistry(const ModuleSet& modules)
        : m_registry(modules)
    {
    }

    /// `provider` declares hook `id` with its default implementation: its own local id, or a declared shared id
    /// (a higher provider of a shared hook wins and receives the lower one as `next`).
    template <typename Signature, typename F>
    Result<void> Declare(std::string_view provider, std::string_view id, F&& implementation)
    {
        return m_registry.Provide(provider, id, Erase<Signature>(std::forward<F>(implementation)));
    }

    /// `provider` overrides a lower module's hook `targetId` (see ContributionRegistry::Override). The signature
    /// must match the declared one.
    template <typename Signature, typename F>
    Result<void> Override(std::string_view provider, std::string_view targetId, F&& implementation)
    {
        INTERI_TRY(CheckSignature<Signature>(targetId));
        return m_registry.Override(provider, targetId, Erase<Signature>(std::forward<F>(implementation)));
    }

    /// A typed handle for `id`. Invalid (and asserting when called) if nobody declares it or the signature differs.
    template <typename Signature>
    [[nodiscard]] HookPoint<Signature> Find(std::string_view id) const
    {
        return HookPoint<Signature>(CheckSignature<Signature>(id).has_value() ? this : nullptr, std::string(id));
    }

    /// The underlying registry: Provenance, change notifications, RemoveProvider.
    [[nodiscard]] ContributionRegistry<detail::ErasedHook>& Registry() noexcept { return m_registry; }
    [[nodiscard]] const ContributionRegistry<detail::ErasedHook>& Registry() const noexcept { return m_registry; }

private:
    template <typename Signature>
    friend class HookPoint;

    template <typename Signature, typename F>
    static detail::ErasedHook Erase(F&& implementation)
    {
        return detail::ErasedHook{.signature = std::string(detail::SignatureName<Signature>()),
                                  .implementation =
                                      std::make_shared<const HookImpl<Signature>>(std::forward<F>(implementation))};
    }

    template <typename Signature>
    [[nodiscard]] Result<void> CheckSignature(std::string_view id) const
    {
        const std::span stack = m_registry.Stack(id);
        if (stack.empty())
        {
            return MakeError(ErrorCode::NotFound, std::format("no hook '{}' is declared", id));
        }
        if (stack.back().value.signature != detail::SignatureName<Signature>())
        {
            return MakeError(ErrorCode::InvalidArgument, std::format("hook '{}' has another signature", id));
        }
        return {};
    }

    ContributionRegistry<detail::ErasedHook> m_registry;
};

/// A typed handle to a hook (HookRegistry::Find). Calling it runs the winning implementation; each one reaches the
/// one it replaced through `next`. Cheap to copy; stays valid while its registry lives (it resolves on every call).
template <typename R, typename... Args>
class HookPoint<R(Args...)>
{
public:
    HookPoint() = default;

    [[nodiscard]] bool IsValid() const noexcept { return m_hooks != nullptr; }
    [[nodiscard]] const std::string& Id() const noexcept { return m_id; }

    R operator()(Args... args) const
    {
        INTERI_ASSERT(m_hooks != nullptr, "calling an invalid HookPoint");
        const std::span stack = m_hooks != nullptr ? m_hooks->m_registry.Stack(m_id) : std::span<const HookEntry>{};
        return HookNext<R(Args...)>(stack, 0)(std::forward<Args>(args)...);
    }

private:
    friend class HookRegistry;

    HookPoint(const HookRegistry* hooks, std::string id)
        : m_hooks(hooks)
        , m_id(std::move(id))
    {
    }

    const HookRegistry* m_hooks = nullptr;
    std::string m_id;
};

/// A hook that creates objects: overrides may replace or wrap what the lower factory makes.
template <typename T>
using FactorySignature = std::unique_ptr<T>();
template <typename T>
using ClassFactory = HookPoint<FactorySignature<T>>;

/// What a module's code registers with: the project's hooks plus its own provider id.
class ModuleScope
{
public:
    ModuleScope(HookRegistry& hooks, std::string provider)
        : m_hooks(&hooks)
        , m_provider(std::move(provider))
    {
    }

    [[nodiscard]] const std::string& Provider() const noexcept { return m_provider; }
    [[nodiscard]] HookRegistry& Hooks() const noexcept { return *m_hooks; }

    /// Declares a hook `id` (usually Local(name)) with its default implementation.
    template <typename Signature, typename F>
    Result<void> DeclareHook(std::string_view id, F&& implementation) const
    {
        return m_hooks->Declare<Signature>(m_provider, id, std::forward<F>(implementation));
    }

    /// Declares a class factory `id` whose default creates `std::make_unique<Default>()`.
    template <typename T, typename Default = T>
    [[nodiscard]] Result<void> DeclareFactory(std::string_view id) const
    {
        return DeclareHook<FactorySignature<T>>(id, [](const HookNext<FactorySignature<T>>&) -> std::unique_ptr<T>
                                                { return std::make_unique<Default>(); });
    }

    /// Overrides `targetId`; the signature is deduced from `implementation` (a function or lambda taking
    /// `const HookNext<R(Args...)>&, Args...`).
    template <typename F>
    Result<void> Override(std::string_view targetId, F&& implementation) const
    {
        using Signature = detail::HookSignatureOf<std::decay_t<F>>::Type;
        return m_hooks->Override<Signature>(m_provider, targetId, std::forward<F>(implementation));
    }

    /// "<provider>:<name>".
    [[nodiscard]] std::string Local(std::string_view name) const { return m_provider + ":" + std::string(name); }

private:
    HookRegistry* m_hooks;
    std::string m_provider;
};

} // namespace interi::sdk

/// Overrides hook `targetId` with `implementation` for the module of `scope` (a ModuleScope); evaluates to a
/// Result<void>. The implementation's first parameter is the `next` handle.
#define INTERI_OVERRIDE(scope, targetId, implementation) (scope).Override((targetId), (implementation))
