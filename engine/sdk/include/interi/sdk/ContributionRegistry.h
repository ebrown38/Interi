// SPDX-License-Identifier: MIT
#pragma once

// Layered contributions: every item a Module, Extension or Plugin provides (hook implementations, class factories,
// later Values, Events, managers...) lives in a ContributionRegistry under an id, as a stack ordered by priority.
//
// Identity rule:
//   - A local id is "<module id>:<name>" ("com.author.fps:FireGun"). Only its own module provides it, so local ids
//     never collide. A higher module or Plugin replaces one only by an explicit Override, which its manifest declares
//     ("overrides": [{ "target": ..., "version": <range of the owner's version> }]).
//   - A shared contract id has no ':' ("service.PhysicsQueries", "input.Jump") and must be declared by the engine or
//     by an Extension's manifest ("contracts"). Any module may provide it; the highest provider wins outright.
//   - Plugins only override: they may provide a shared id that already has a provider, or Override a local id.
// Resolve(id) is the top of the stack; Provenance(id) lists the winner and every shadowed provider. Not thread-safe
// to modify; concurrent reads are fine while nothing modifies it.

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::sdk
{

/// The engine's own provider id: below every module.
inline constexpr std::string_view kEngineProviderId = "interi.engine";

/// Someone who contributes items.
struct Provider
{
    std::string id;
    ModuleKind kind = ModuleKind::Module;
    SemVer version;
    /// 0 for the engine; a module's graph priority + 1.
    std::uint32_t rank = 0;
    std::vector<OverrideDeclaration> overrides;
};

/// The providers of a resolved project plus the declared shared contract ids: what every registry checks
/// contributions against.
class ModuleSet
{
public:
    /// Just the engine.
    ModuleSet();
    /// The engine plus every module of `graph`. `engineContracts` are the shared ids the engine declares; every
    /// Extension's "contracts" are added.
    ModuleSet(const ModuleGraph& graph, std::span<const std::string> engineContracts);

    /// Adds a provider (tests, and the host while it loads a project). Replaces one with the same id.
    void AddProvider(Provider provider);
    void DeclareContract(std::string id) { m_contracts.push_back(std::move(id)); }

    [[nodiscard]] const Provider* Find(std::string_view id) const noexcept;
    [[nodiscard]] bool IsContract(std::string_view id) const noexcept;

private:
    std::vector<Provider> m_providers;
    std::vector<std::string> m_contracts;
};

/// One provider of an id, as Provenance lists it.
struct ProvenanceEntry
{
    std::string provider;
    std::uint32_t rank = 0;
    bool isOverride = false; ///< Contributed with Override (a local id's replacement).
};

/// RAII handle for a change subscription; unsubscribes on destruction. Move-only.
class Subscription
{
public:
    Subscription() = default;
    Subscription(std::function<void(std::uint64_t)> cancel, std::uint64_t token)
        : m_cancel(std::move(cancel))
        , m_token(token)
    {
    }
    Subscription(Subscription&& other) noexcept
        : m_cancel(std::exchange(other.m_cancel, nullptr))
        , m_token(other.m_token)
    {
    }
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_cancel = std::exchange(other.m_cancel, nullptr);
            m_token = other.m_token;
        }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { Reset(); }

    void Reset()
    {
        if (m_cancel)
        {
            std::exchange(m_cancel, nullptr)(m_token);
        }
    }

private:
    std::function<void(std::uint64_t)> m_cancel;
    std::uint64_t m_token = 0;
};

namespace detail
{

/// The id rules for one contribution. `existing` are the current providers of `id` (any order). Returns the
/// contributor's rank.
enum class ContributionKind : std::uint8_t
{
    Provide,
    Override
};
struct ExistingEntry
{
    std::string_view provider;
    std::uint32_t rank;
};
[[nodiscard]] Result<std::uint32_t> CheckContribution(const ModuleSet& modules, std::string_view provider,
                                                      std::string_view id, ContributionKind kind,
                                                      std::span<const ExistingEntry> existing);

/// Change subscribers of one registry.
class ChangeListeners
{
public:
    using Callback = std::function<void(std::string_view id)>;

    std::uint64_t Add(Callback callback);
    void Remove(std::uint64_t token);
    void Notify(std::string_view id) const;

private:
    std::vector<std::pair<std::uint64_t, Callback>> m_callbacks;
    std::uint64_t m_nextToken = 1;
};

} // namespace detail

/// A registry of items of type T (see the top of this header).
template <typename T>
class ContributionRegistry
{
public:
    struct Entry
    {
        std::string provider;
        std::uint32_t rank = 0;
        bool isOverride = false;
        T value;
    };

    /// `modules` must outlive the registry.
    explicit ContributionRegistry(const ModuleSet& modules)
        : m_modules(&modules)
    {
    }

    ContributionRegistry(const ContributionRegistry&) = delete;
    ContributionRegistry& operator=(const ContributionRegistry&) = delete;
    ContributionRegistry(ContributionRegistry&&) = delete;
    ContributionRegistry& operator=(ContributionRegistry&&) = delete;
    ~ContributionRegistry() = default;

    /// `provider` provides `id` (its own local id, or a declared shared id).
    Result<void> Provide(std::string_view provider, std::string_view id, T value)
    {
        return Add(provider, id, detail::ContributionKind::Provide, std::move(value));
    }

    /// `provider` replaces a lower module's local item `targetId` (declared in its manifest's "overrides").
    Result<void> Override(std::string_view provider, std::string_view targetId, T value)
    {
        return Add(provider, targetId, detail::ContributionKind::Override, std::move(value));
    }

    /// The winning item, or nullptr if nobody provides `id`.
    [[nodiscard]] const T* Resolve(std::string_view id) const noexcept
    {
        const std::vector<Entry>* stack = Find(id);
        return stack == nullptr || stack->empty() ? nullptr : &stack->front().value;
    }

    /// The winner first, then every shadowed provider by descending priority. Empty if nobody provides `id`.
    [[nodiscard]] std::vector<ProvenanceEntry> Provenance(std::string_view id) const
    {
        std::vector<ProvenanceEntry> result;
        if (const std::vector<Entry>* stack = Find(id))
        {
            for (const Entry& entry : *stack)
            {
                result.push_back({.provider = entry.provider, .rank = entry.rank, .isOverride = entry.isOverride});
            }
        }
        return result;
    }

    /// Every provider's entry for `id`, winner first (hooks walk it for `next`).
    [[nodiscard]] std::span<const Entry> Stack(std::string_view id) const noexcept
    {
        const std::vector<Entry>* stack = Find(id);
        return stack == nullptr ? std::span<const Entry>{} : std::span<const Entry>(*stack);
    }

    /// Every id with at least one provider, sorted.
    [[nodiscard]] std::vector<std::string> Ids() const
    {
        std::vector<std::string> ids;
        for (const auto& [id, stack] : m_items)
        {
            if (!stack.empty())
            {
                ids.push_back(id);
            }
        }
        return ids;
    }

    /// Removes everything `provider` contributed (its module is unloading). Notifies ids whose winner changed.
    void RemoveProvider(std::string_view provider)
    {
        for (auto& [id, stack] : m_items)
        {
            const bool wasWinner = !stack.empty() && stack.front().provider == provider;
            std::erase_if(stack, [&](const Entry& entry) { return entry.provider == provider; });
            if (wasWinner)
            {
                m_listeners.Notify(id);
            }
        }
        std::erase_if(m_items, [](const auto& item) { return item.second.empty(); });
    }

    /// Calls `callback(id)` whenever the winner of an id changes (a new top provider, or the top one removed).
    [[nodiscard]] Subscription Subscribe(std::function<void(std::string_view id)> callback)
    {
        const std::uint64_t token = m_listeners.Add(std::move(callback));
        return Subscription([this](std::uint64_t t) { m_listeners.Remove(t); }, token);
    }

private:
    [[nodiscard]] const std::vector<Entry>* Find(std::string_view id) const noexcept
    {
        const auto it = m_items.find(id);
        return it == m_items.end() ? nullptr : &it->second;
    }

    Result<void> Add(std::string_view provider, std::string_view id, detail::ContributionKind kind, T value)
    {
        std::vector<detail::ExistingEntry> existing;
        const auto it = m_items.find(id);
        if (it != m_items.end())
        {
            for (const Entry& entry : it->second)
            {
                existing.push_back({.provider = entry.provider, .rank = entry.rank});
            }
        }
        INTERI_TRY_ASSIGN(rank, detail::CheckContribution(*m_modules, provider, id, kind, existing));

        std::vector<Entry>& stack = it != m_items.end() ? it->second : m_items[std::string(id)];
        const auto position =
            std::ranges::find_if(stack, [rankValue = rank](const Entry& entry) { return entry.rank < rankValue; });
        const bool newWinner = position == stack.begin();
        stack.insert(position, Entry{.provider = std::string(provider),
                                     .rank = rank,
                                     .isOverride = kind == detail::ContributionKind::Override,
                                     .value = std::move(value)});
        if (newWinner)
        {
            m_listeners.Notify(id);
        }
        return {};
    }

    const ModuleSet* m_modules;
    std::map<std::string, std::vector<Entry>, std::less<>> m_items;
    detail::ChangeListeners m_listeners;
};

} // namespace interi::sdk
