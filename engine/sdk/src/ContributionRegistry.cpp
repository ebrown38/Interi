// SPDX-License-Identifier: MIT
#include <interi/sdk/ContributionRegistry.h>

#include <format>

namespace interi::sdk
{

std::vector<std::string> PhysicsContracts()
{
    return {"service.PhysicsQueries"};
}

ModuleSet::ModuleSet()
{
    m_providers.push_back(Provider{.id = std::string(kEngineProviderId),
                                   .kind = ModuleKind::Extension,
                                   .version = CurrentHost().engineVersion,
                                   .rank = 0,
                                   .overrides = {}});
}

ModuleSet::ModuleSet(const ModuleGraph& graph, std::span<const std::string> engineContracts)
    : ModuleSet()
{
    for (const std::string& contract : engineContracts)
    {
        DeclareContract(contract);
    }
    for (const ResolvedModule& module : graph.modules)
    {
        AddProvider(Provider{.id = module.manifest.id,
                             .kind = module.manifest.kind,
                             .version = module.manifest.version,
                             .rank = module.priority + 1,
                             .overrides = module.manifest.overrides});
        for (const std::string& contract : module.manifest.contracts)
        {
            DeclareContract(contract);
        }
    }
}

void ModuleSet::AddProvider(Provider provider)
{
    for (Provider& existing : m_providers)
    {
        if (existing.id == provider.id)
        {
            existing = std::move(provider);
            return;
        }
    }
    m_providers.push_back(std::move(provider));
}

const Provider* ModuleSet::Find(std::string_view id) const noexcept
{
    for (const Provider& provider : m_providers)
    {
        if (provider.id == id)
        {
            return &provider;
        }
    }
    return nullptr;
}

bool ModuleSet::IsContract(std::string_view id) const noexcept
{
    return std::ranges::find(m_contracts, id) != m_contracts.end();
}

namespace detail
{

Result<std::uint32_t> CheckContribution(const ModuleSet& modules, std::string_view provider, std::string_view id,
                                        ContributionKind kind, std::span<const ExistingEntry> existing)
{
    const Provider* const contributor = modules.Find(provider);
    if (contributor == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("unknown provider '{}' for '{}'", provider, id));
    }
    for (const ExistingEntry& entry : existing)
    {
        if (entry.provider == provider)
        {
            return MakeError(ErrorCode::InvalidArgument, std::format("'{}' already provides '{}'", provider, id));
        }
    }
    if (id.starts_with(kPhysicsContractPrefix) && contributor->kind != ModuleKind::PhysicsEngine)
    {
        return MakeError(
            ErrorCode::InvalidArgument,
            std::format("'{}' is reserved for the selected Physics Engine; '{}' can't provide it", id, provider));
    }
    const bool isPlugin = contributor->kind == ModuleKind::Plugin;

    if (IsLocalId(id))
    {
        const std::string_view owner = OwnerOf(id);
        if (kind == ContributionKind::Provide)
        {
            if (owner != provider)
            {
                return MakeError(ErrorCode::InvalidArgument,
                                 std::format("'{}' is a local id of '{}'; '{}' can only replace it with an explicit "
                                             "override",
                                             id, owner, provider));
            }
            if (isPlugin)
            {
                return MakeError(ErrorCode::InvalidArgument,
                                 std::format("Plugin '{}' can only override items, not provide '{}'", provider, id));
            }
            return contributor->rank;
        }

        if (owner == provider)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("'{}' can't override its own item '{}'", provider, id));
        }
        const bool ownerProvides =
            std::ranges::any_of(existing, [&](const ExistingEntry& entry) { return entry.provider == owner; });
        const Provider* const ownerProvider = modules.Find(owner);
        if (!ownerProvides || ownerProvider == nullptr)
        {
            return MakeError(ErrorCode::NotFound,
                             std::format("'{}' overrides '{}', which no loaded module provides", provider, id));
        }
        const auto declaration =
            std::ranges::find_if(contributor->overrides, [&](const OverrideDeclaration& d) { return d.target == id; });
        if (declaration == contributor->overrides.end())
        {
            return MakeError(
                ErrorCode::InvalidArgument,
                std::format("'{}' overrides '{}' without declaring it in its manifest's \"overrides\"", provider, id));
        }
        if (!declaration->version.Matches(ownerProvider->version))
        {
            return MakeError(ErrorCode::VersionMismatch,
                             std::format("'{}' overrides '{}' for '{}' {}, but {} is loaded", provider, id, owner,
                                         declaration->version.Text(), ownerProvider->version.ToString()));
        }
        if (contributor->rank <= ownerProvider->rank)
        {
            return MakeError(
                ErrorCode::InvalidArgument,
                std::format("'{}' overrides '{}', but '{}' has higher priority; place it above", provider, id, owner));
        }
        return contributor->rank;
    }

    if (id.find(':') != std::string_view::npos || !IsValidItemName(id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("'{}' is not a valid item id", id));
    }
    if (kind == ContributionKind::Override)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is a shared id: provide it instead (the highest provider wins)", id));
    }
    if (!modules.IsContract(id))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' provides '{}', which is neither its local id ('{}:<name>') nor a declared "
                                     "shared contract id",
                                     provider, id, provider));
    }
    if (isPlugin && existing.empty())
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("Plugin '{}' can only override '{}', which nobody provides", provider, id));
    }
    return contributor->rank;
}

std::uint64_t ChangeListeners::Add(Callback callback)
{
    const std::uint64_t token = m_nextToken++;
    m_callbacks.emplace_back(token, std::move(callback));
    return token;
}

void ChangeListeners::Remove(std::uint64_t token)
{
    std::erase_if(m_callbacks, [token](const auto& entry) { return entry.first == token; });
}

void ChangeListeners::Notify(std::string_view id) const
{
    // Copy: a callback may subscribe or unsubscribe.
    const auto callbacks = m_callbacks;
    for (const auto& [token, callback] : callbacks)
    {
        callback(id);
    }
}

} // namespace detail

} // namespace interi::sdk
