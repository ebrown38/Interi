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

} // namespace

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
                                       std::span<const ModuleManifest> available, const HostInfo& host)
{
    std::map<std::string, const ModuleManifest*, std::less<>> byId;
    for (const ModuleManifest& manifest : available)
    {
        if (!byId.emplace(manifest.id, &manifest).second)
        {
            return Invalid(std::format("module '{}' was found twice", manifest.id));
        }
    }

    // The included set: the project's modules, then required dependencies (breadth first, in declaration order).
    std::vector<const ModuleManifest*> included;
    std::vector<bool> implicit;
    std::map<std::string, std::size_t, std::less<>> indexOf;
    for (const std::string& id : projectOrder)
    {
        const auto it = byId.find(id);
        if (it == byId.end())
        {
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

    ModuleGraph graph;
    graph.modules.reserve(order.size());
    for (std::size_t position = 0; position < order.size(); ++position)
    {
        const std::size_t i = order[position];
        graph.modules.push_back({.manifest = *included[i],
                                 .implicit = implicit[i],
                                 .priority = static_cast<std::uint32_t>(order.size() - 1 - position)});
    }

    // Load order: Kahn's algorithm, always taking the ready module of lowest priority (the bottom of the list).
    std::vector<std::size_t> remainingDependencies(graph.modules.size());
    for (std::size_t m = 0; m < graph.modules.size(); ++m)
    {
        for (const ModuleDependency& dependency : graph.modules[m].manifest.dependencies)
        {
            if (indexOf.contains(dependency.id))
            {
                ++remainingDependencies[m];
            }
        }
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
            for (const ModuleDependency& dependency : graph.modules[m].manifest.dependencies)
            {
                if (dependency.id == graph.modules[next].manifest.id)
                {
                    --remainingDependencies[m];
                }
            }
        }
    }
    return graph;
}

} // namespace interi::sdk
