// SPDX-License-Identifier: MIT
#include <interi/sdk/ModuleGraph.h>

#include <algorithm>
#include <format>
#include <map>

namespace interi::sdk
{
namespace
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

/// Finds a dependency cycle among `included` (ids → manifests) with DFS; returns it as "a -> b -> a", or empty.
std::string FindCycle(const std::vector<const ModuleManifest*>& included,
                      const std::map<std::string, std::size_t, std::less<>>& indexOf)
{
    enum class Mark : std::uint8_t
    {
        None,
        Active,
        Done
    };
    std::vector<Mark> marks(included.size(), Mark::None);
    std::vector<std::size_t> path;

    // Iterative DFS keeps stack depth independent of the project size.
    struct Frame
    {
        std::size_t node;
        std::size_t nextDependency;
    };
    for (std::size_t start = 0; start < included.size(); ++start)
    {
        if (marks[start] != Mark::None)
        {
            continue;
        }
        std::vector<Frame> stack{{.node = start, .nextDependency = 0}};
        marks[start] = Mark::Active;
        path.push_back(start);
        while (!stack.empty())
        {
            Frame& frame = stack.back();
            const auto& dependencies = included[frame.node]->dependencies;
            if (frame.nextDependency == dependencies.size())
            {
                marks[frame.node] = Mark::Done;
                path.pop_back();
                stack.pop_back();
                continue;
            }
            const auto it = indexOf.find(dependencies[frame.nextDependency++].id);
            if (it == indexOf.end())
            {
                continue; // an absent optional dependency
            }
            const std::size_t next = it->second;
            if (marks[next] == Mark::Active)
            {
                std::string cycle;
                const auto begin = std::ranges::find(path, next);
                for (auto node = begin; node != path.end(); ++node)
                {
                    cycle += included[*node]->id;
                    cycle += " -> ";
                }
                cycle += included[next]->id;
                return cycle;
            }
            if (marks[next] == Mark::None)
            {
                marks[next] = Mark::Active;
                path.push_back(next);
                stack.push_back({.node = next, .nextDependency = 0});
            }
        }
    }
    return {};
}

using EngineList = std::vector<const ModuleManifest*>;

/// Installed PhysicsEngine manifests with `id`, highest version first.
EngineList InstalledEngines(std::span<const ModuleManifest> available, std::string_view id)
{
    EngineList result;
    for (const ModuleManifest& manifest : available)
    {
        if (manifest.kind == ModuleKind::PhysicsEngine && manifest.id == id)
        {
            result.push_back(&manifest);
        }
    }
    std::ranges::sort(result, [](const ModuleManifest* a, const ModuleManifest* b) { return a->version > b->version; });
    return result;
}

/// Every chain starting with the engine `first`, bases chosen highest version first (so the preferred chain is
/// first). Structural problems are errors; a base with no version in the extends range just yields no chain.
Result<void> BuildChains(std::span<const ModuleManifest> available, const HostInfo& host, EngineList& prefix,
                         std::vector<EngineList>& chains)
{
    const ModuleManifest& top = *prefix.back();
    if (!top.extends)
    {
        chains.push_back(prefix);
        return {};
    }
    const PhysicsEngineRef& base = *top.extends;
    if (std::ranges::any_of(prefix, [&](const ModuleManifest* m) { return m->id == base.id; }))
    {
        std::string cycle;
        for (const ModuleManifest* member : prefix)
        {
            cycle += member->id + " -> ";
        }
        return Invalid(std::format("Physics Engine extends cycle: {}{}", cycle, base.id));
    }
    const EngineList versions = InstalledEngines(available, base.id);
    if (versions.empty())
    {
        const bool exists = std::ranges::any_of(available, [&](const ModuleManifest& m) { return m.id == base.id; });
        if (exists)
        {
            return Invalid(
                std::format("Physics Engine '{}' extends '{}', which is not a PhysicsEngine", top.id, base.id));
        }
        return MakeError(ErrorCode::NotFound,
                         std::format("Physics Engine '{}' extends '{}', which was not found", top.id, base.id));
    }
    for (const ModuleManifest* version : versions)
    {
        if (!base.version.Matches(version->version) || !CheckHostCompatibility(*version, host))
        {
            continue;
        }
        prefix.push_back(version);
        const auto built = BuildChains(available, host, prefix, chains);
        prefix.pop_back();
        INTERI_TRY(built);
    }
    return {};
}

bool SupportedBy(const EngineList& chain, const ModuleManifest& module)
{
    return std::ranges::any_of(module.physicsEngines,
                               [&](const PhysicsEngineEntry& entry) { return Satisfies(chain, entry); });
}

} // namespace

std::string_view ToString(PhysicsEngineReason reason) noexcept
{
    switch (reason)
    {
    case PhysicsEngineReason::None:
        return "none";
    case PhysicsEngineReason::Pinned:
        return "pinned by the project";
    case PhysicsEngineReason::Default:
        return "default";
    case PhysicsEngineReason::Fallback:
        return "fallback";
    }
    return "none";
}

bool Satisfies(std::span<const ModuleManifest* const> chain, const PhysicsEngineEntry& entry) noexcept
{
    return std::ranges::any_of(chain, [&](const ModuleManifest* engine)
                               { return engine->id == entry.id && entry.version.Matches(engine->version); });
}

Result<std::optional<PhysicsEngineSelection>> SelectPhysicsEngine(std::span<const ModuleManifest* const> modules,
                                                                  std::span<const ModuleManifest> available,
                                                                  const std::optional<PhysicsEngineRef>& pin,
                                                                  const HostInfo& host)
{
    struct Candidate
    {
        std::string id;
        std::string nominatedBy;
        bool isDefault = false;
    };
    std::vector<const ModuleManifest*> declaring;
    std::vector<Candidate> candidates;
    const auto addCandidate = [&](const PhysicsEngineEntry& entry, const ModuleManifest& module)
    {
        if (!std::ranges::any_of(candidates, [&](const Candidate& c) { return c.id == entry.id; }))
        {
            candidates.push_back({.id = entry.id, .nominatedBy = module.id, .isDefault = entry.isDefault});
        }
    };
    for (const ModuleManifest* module : modules)
    {
        if (module->physicsEngines.empty())
        {
            continue;
        }
        declaring.push_back(module);
        for (const PhysicsEngineEntry& entry : module->physicsEngines)
        {
            if (entry.isDefault)
            {
                addCandidate(entry, *module);
            }
        }
        for (const PhysicsEngineEntry& entry : module->physicsEngines)
        {
            if (!entry.isDefault)
            {
                addCandidate(entry, *module);
            }
        }
    }
    if (pin)
    {
        candidates.assign(1, Candidate{.id = pin->id, .nominatedBy = {}, .isDefault = false});
    }
    if (candidates.empty())
    {
        return std::optional<PhysicsEngineSelection>{};
    }

    std::vector<std::string> report;
    bool anyInstalled = false;
    for (const Candidate& candidate : candidates)
    {
        const EngineList versions = InstalledEngines(available, candidate.id);
        std::string blockers;
        bool tried = false;
        for (const ModuleManifest* version : versions)
        {
            anyInstalled = true;
            if (!CheckHostCompatibility(*version, host) || (pin && !pin->version.Matches(version->version)))
            {
                continue;
            }
            std::vector<EngineList> chains;
            EngineList prefix{version};
            INTERI_TRY(BuildChains(available, host, prefix, chains));
            for (const EngineList& chain : chains)
            {
                const auto blocked = std::ranges::find_if(declaring, [&](const ModuleManifest* module)
                                                          { return !SupportedBy(chain, *module); });
                if (blocked == declaring.end())
                {
                    return std::optional<PhysicsEngineSelection>{PhysicsEngineSelection{
                        .chain = chain,
                        .reason =
                            pin ? PhysicsEngineReason::Pinned
                                : (candidate.isDefault ? PhysicsEngineReason::Default : PhysicsEngineReason::Fallback),
                        .chosenBy = candidate.nominatedBy}};
                }
                if (!tried)
                {
                    // Report the preferred chain (highest version) of the candidate.
                    for (const ModuleManifest* module : declaring)
                    {
                        if (!SupportedBy(chain, *module))
                        {
                            blockers += (blockers.empty() ? "" : ", ") + module->id;
                        }
                    }
                    tried = true;
                }
            }
        }
        if (versions.empty())
        {
            report.push_back(std::format("'{}' is not installed", candidate.id));
        }
        else if (!tried)
        {
            report.push_back(std::format("'{}' has no installed version this engine can use{}", candidate.id,
                                         pin ? std::format(" in {}", pin->version.Text()) : std::string{}));
        }
        else
        {
            report.push_back(std::format("'{}' is not supported by {}", candidate.id, blockers));
        }
    }
    std::string message = "no Physics Engine works for this project: ";
    for (std::size_t i = 0; i < report.size(); ++i)
    {
        message += (i == 0 ? "" : "; ") + report[i];
    }
    return MakeError(anyInstalled ? ErrorCode::VersionMismatch : ErrorCode::NotFound, std::move(message));
}

std::size_t ModuleGraph::Find(std::string_view id) const noexcept
{
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        if (modules[i].manifest.id == id)
        {
            return i;
        }
    }
    return npos;
}

const ResolvedModule* ModuleGraph::Get(std::string_view id) const noexcept
{
    const std::size_t index = Find(id);
    return index == npos ? nullptr : &modules[index];
}

Result<ModuleGraph> ResolveModuleGraph(std::span<const std::string> projectOrder,
                                       std::span<const ModuleManifest> available, const HostInfo& host,
                                       const std::optional<PhysicsEngineRef>& physicsEngine)
{
    // Physics Engines are only reached through `physicsEngines` and the pin; several versions of one may be installed.
    std::map<std::string, const ModuleManifest*, std::less<>> byId;
    std::map<std::string, std::vector<const ModuleManifest*>, std::less<>> engines;
    for (const ModuleManifest& manifest : available)
    {
        if (manifest.kind == ModuleKind::PhysicsEngine)
        {
            if (byId.contains(manifest.id))
            {
                return Invalid(std::format("module '{}' was found twice", manifest.id));
            }
            auto& versions = engines[manifest.id];
            if (std::ranges::any_of(versions, [&](const ModuleManifest* m) { return m->version == manifest.version; }))
            {
                return Invalid(
                    std::format("Physics Engine '{}' {} was found twice", manifest.id, manifest.version.ToString()));
            }
            versions.push_back(&manifest);
        }
        else if (!byId.emplace(manifest.id, &manifest).second || engines.contains(manifest.id))
        {
            return Invalid(std::format("module '{}' was found twice", manifest.id));
        }
    }
    const auto engineError = [](std::string_view user, std::string_view engine)
    {
        return Invalid(std::format("{} names Physics Engine '{}', which can't be listed or depended on; declare it in "
                                   "\"physicsEngines\" (or pin it in the project)",
                                   user, engine));
    };

    // The included set: the project's modules, then required dependencies (breadth first, in declaration order).
    std::vector<const ModuleManifest*> included;
    std::vector<bool> implicit;
    std::map<std::string, std::size_t, std::less<>> indexOf;
    for (const std::string& id : projectOrder)
    {
        const auto it = byId.find(id);
        if (it == byId.end())
        {
            if (engines.contains(id))
            {
                return engineError("the project", id);
            }
            return MakeError(ErrorCode::NotFound, std::format("project module '{}' was not found", id));
        }
        if (!indexOf.emplace(id, included.size()).second)
        {
            return Invalid(std::format("module '{}' is listed twice in the project", id));
        }
        included.push_back(it->second);
        implicit.push_back(false);
    }
    for (std::size_t i = 0; i < included.size(); ++i)
    {
        for (const ModuleDependency& dependency : included[i]->dependencies)
        {
            if (dependency.optional || indexOf.contains(dependency.id))
            {
                continue;
            }
            const auto it = byId.find(dependency.id);
            if (it == byId.end())
            {
                if (engines.contains(dependency.id))
                {
                    return engineError(std::format("module '{}'", included[i]->id), dependency.id);
                }
                return MakeError(ErrorCode::NotFound, std::format("module '{}' needs '{}', which was not found",
                                                                  included[i]->id, dependency.id));
            }
            indexOf.emplace(dependency.id, included.size());
            included.push_back(it->second);
            implicit.push_back(true);
        }
    }

    // Per-module checks: the host, dependency kinds and versions, conflicts.
    for (const ModuleManifest* manifest : included)
    {
        INTERI_TRY(CheckHostCompatibility(*manifest, host));
        for (const ModuleDependency& dependency : manifest->dependencies)
        {
            const auto it = indexOf.find(dependency.id);
            if (it == indexOf.end())
            {
                continue; // optional and absent
            }
            const ModuleManifest& target = *included[it->second];
            if (target.kind != ModuleKind::Extension)
            {
                return Invalid(std::format("module '{}' depends on '{}', which is a {}; only Extensions can be "
                                           "depended on",
                                           manifest->id, target.id, ToString(target.kind)));
            }
            if (!dependency.version.Matches(target.version))
            {
                return MakeError(ErrorCode::VersionMismatch,
                                 std::format("module '{}' needs '{}' {}, found {}", manifest->id, target.id,
                                             dependency.version.Text(), target.version.ToString()));
            }
        }
        for (const ModuleConflict& conflict : manifest->conflicts)
        {
            const auto it = indexOf.find(conflict.id);
            if (it != indexOf.end() && conflict.version.Matches(included[it->second]->version))
            {
                return Invalid(std::format("module '{}' declares a conflict with '{}' {}", manifest->id, conflict.id,
                                           included[it->second]->version.ToString()));
            }
        }
    }

    if (const std::string cycle = FindCycle(included, indexOf); !cycle.empty())
    {
        return Invalid(std::format("dependency cycle: {}", cycle));
    }

    // Priority order: the project's list, then each implicit module directly below its lowest dependent. An implicit
    // module is placed once every dependent is placed (no cycles, so this terminates), in discovery order.
    std::vector<std::size_t> order;
    order.reserve(included.size());
    for (std::size_t i = 0; i < included.size(); ++i)
    {
        if (!implicit[i])
        {
            order.push_back(i);
        }
    }
    std::vector<bool> placed(included.size());
    for (const std::size_t i : order)
    {
        placed[i] = true;
    }
    while (order.size() < included.size())
    {
        bool progress = false;
        for (std::size_t i = 0; i < included.size(); ++i)
        {
            if (placed[i])
            {
                continue;
            }
            // Dependents of i, and whether all are placed.
            std::size_t lowestPosition = 0;
            bool ready = true;
            for (std::size_t j = 0; j < included.size() && ready; ++j)
            {
                const auto& dependencies = included[j]->dependencies;
                const bool dependsOnI = std::ranges::any_of(dependencies, [&](const ModuleDependency& d)
                                                            { return d.id == included[i]->id; });
                if (!dependsOnI)
                {
                    continue;
                }
                if (!placed[j])
                {
                    ready = false;
                    break;
                }
                const auto position = static_cast<std::size_t>(std::ranges::find(order, j) - order.begin());
                lowestPosition = std::max(lowestPosition, position);
            }
            if (!ready)
            {
                continue;
            }
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(lowestPosition + 1), i);
            placed[i] = true;
            progress = true;
        }
        if (!progress)
        {
            return Invalid("internal error placing implicit modules"); // unreachable without a cycle
        }
    }

    // The Physics Engine: chosen among the modules in priority order, then placed below the lowest declaring one.
    std::vector<const ModuleManifest*> inPriority;
    inPriority.reserve(order.size());
    for (const std::size_t i : order)
    {
        inPriority.push_back(included[i]);
    }
    INTERI_TRY_ASSIGN(selection, SelectPhysicsEngine(inPriority, available, physicsEngine, host));
    std::size_t chainStart = ModuleGraph::npos;
    if (selection)
    {
        std::size_t insertAt = 0;
        for (std::size_t position = 0; position < order.size(); ++position)
        {
            if (!included[order[position]]->physicsEngines.empty())
            {
                insertAt = position + 1;
            }
        }
        if (insertAt == 0)
        {
            insertAt = order.size(); // pinned with nobody declaring: the bottom
        }
        chainStart = insertAt;
        for (std::size_t k = 0; k < selection->chain.size(); ++k)
        {
            const ModuleManifest* engine = selection->chain[k];
            for (const ModuleManifest* other : inPriority)
            {
                const auto clash =
                    std::ranges::find_if(engine->conflicts, [&](const ModuleConflict& c)
                                         { return c.id == other->id && c.version.Matches(other->version); });
                const auto reverse =
                    std::ranges::find_if(other->conflicts, [&](const ModuleConflict& c)
                                         { return c.id == engine->id && c.version.Matches(engine->version); });
                if (clash != engine->conflicts.end() || reverse != other->conflicts.end())
                {
                    return Invalid(std::format("module '{}' declares a conflict with Physics Engine '{}'",
                                               clash != engine->conflicts.end() ? engine->id : other->id,
                                               clash != engine->conflicts.end() ? other->id : engine->id));
                }
            }
            indexOf.emplace(engine->id, included.size());
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(insertAt + k), included.size());
            included.push_back(engine);
            implicit.push_back(true);
        }
    }

    ModuleGraph graph;
    graph.modules.reserve(order.size());
    for (std::size_t position = 0; position < order.size(); ++position)
    {
        const std::size_t i = order[position];
        graph.modules.push_back({.manifest = *included[i],
                                 .implicit = implicit[i],
                                 .priority = static_cast<std::uint32_t>(order.size() - 1 - position),
                                 .loadAfter = {}});
    }
    if (selection)
    {
        graph.physicsEngine = chainStart;
        for (std::size_t k = 0; k < selection->chain.size(); ++k)
        {
            graph.physicsEngineChain.push_back(chainStart + k);
        }
        graph.physicsEngineReason = selection->reason;
        graph.physicsEngineChosenBy = selection->chosenBy;
    }

    // What must load before each module: its dependencies present in the project, and the engine edges.
    for (ResolvedModule& module : graph.modules)
    {
        for (const ModuleDependency& dependency : module.manifest.dependencies)
        {
            if (indexOf.contains(dependency.id))
            {
                module.loadAfter.push_back(dependency.id);
            }
        }
        if (selection && !module.manifest.physicsEngines.empty())
        {
            module.loadAfter.push_back(selection->chain.front()->id);
        }
    }
    if (selection)
    {
        for (std::size_t k = 0; k + 1 < selection->chain.size(); ++k)
        {
            graph.modules[chainStart + k].loadAfter.push_back(selection->chain[k + 1]->id);
        }
    }

    // Load order: Kahn's algorithm, always taking the ready module of lowest priority (the bottom of the list).
    std::vector<std::size_t> remainingDependencies(graph.modules.size());
    for (std::size_t m = 0; m < graph.modules.size(); ++m)
    {
        remainingDependencies[m] = graph.modules[m].loadAfter.size();
    }
    std::vector<bool> loaded(graph.modules.size());
    while (graph.loadOrder.size() < graph.modules.size())
    {
        std::size_t next = ModuleGraph::npos;
        for (std::size_t m = graph.modules.size(); m-- > 0;)
        {
            if (!loaded[m] && remainingDependencies[m] == 0)
            {
                next = m;
                break;
            }
        }
        if (next == ModuleGraph::npos)
        {
            return Invalid("internal error ordering modules"); // unreachable without a cycle
        }
        loaded[next] = true;
        graph.loadOrder.push_back(next);
        for (std::size_t m = 0; m < graph.modules.size(); ++m)
        {
            remainingDependencies[m] -= static_cast<std::size_t>(
                std::ranges::count(graph.modules[m].loadAfter, graph.modules[next].manifest.id));
        }
    }
    return graph;
}

} // namespace interi::sdk

namespace interi::sdk
{

Result<void> CanAddModule(const Project& project, std::span<const ModuleManifest> available,
                          const ModuleManifest& candidate, const HostInfo& host, std::size_t position)
{
    std::vector<ModuleManifest> modules(available.begin(), available.end());
    std::erase_if(modules,
                  [&](const ModuleManifest& m) { return m.id == candidate.id && m.kind != ModuleKind::PhysicsEngine; });
    modules.push_back(candidate);
    std::vector<std::string> order = project.modules;
    if (std::ranges::contains(order, candidate.id))
    {
        return Invalid(std::format("module '{}' is already in the project", candidate.id));
    }
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(std::min(position, order.size())), candidate.id);
    INTERI_TRY_ASSIGN(graph, ResolveModuleGraph(order, modules, host, project.physicsEngine));
    return {};
}

} // namespace interi::sdk
