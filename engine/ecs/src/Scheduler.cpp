// SPDX-License-Identifier: MIT
#include <interi/ecs/Scheduler.h>

#include <algorithm>

namespace interi
{

Scheduler::SystemId Scheduler::AddSystem(std::string name, SystemAccess access, SystemFunction function)
{
    m_systems.push_back(System{.name = std::move(name), .access = access, .function = std::move(function)});
    m_built = false;
    return static_cast<SystemId>(m_systems.size() - 1);
}

void Scheduler::RunAfter(SystemId system, SystemId dependency)
{
    INTERI_ASSERT(system < m_systems.size() && dependency < m_systems.size(), "RunAfter: unknown system");
    m_explicit.emplace_back(dependency, system);
    m_built = false;
}

bool Scheduler::Conflicts(SystemId a, SystemId b) const
{
    const SystemAccess& x = m_systems[a].access;
    const SystemAccess& y = m_systems[b].access;
    return (x.writes & (y.reads | y.writes)).any() || (y.writes & x.reads).any();
}

bool Scheduler::Reaches(SystemId from, SystemId to) const
{
    std::vector<bool> visited(m_systems.size(), false);
    std::vector<SystemId> stack{from};
    while (!stack.empty())
    {
        const SystemId node = stack.back();
        stack.pop_back();
        if (node == to)
        {
            return true;
        }
        for (const SystemId next : m_successors[node])
        {
            if (!visited[next])
            {
                visited[next] = true;
                stack.push_back(next);
            }
        }
    }
    return false;
}

Result<void> Scheduler::Build()
{
    m_built = false;
    const std::size_t count = m_systems.size();
    m_successors.assign(count, {});
    std::vector<std::uint32_t> indegree(count, 0);
    for (const auto& [before, after] : m_explicit)
    {
        m_successors[before].push_back(after);
        ++indegree[after];
    }

    // Kahn's algorithm on the explicit orders; whatever never reaches indegree zero is on or behind a cycle.
    std::vector<SystemId> ready;
    for (SystemId system = 0; system < count; ++system)
    {
        if (indegree[system] == 0)
        {
            ready.push_back(system);
        }
    }
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const SystemId node = ready.back();
        ready.pop_back();
        ++visited;
        for (const SystemId next : m_successors[node])
        {
            if (--indegree[next] == 0)
            {
                ready.push_back(next);
            }
        }
    }
    if (visited != count)
    {
        std::string names;
        for (SystemId system = 0; system < count; ++system)
        {
            if (indegree[system] != 0)
            {
                names += (names.empty() ? "" : ", ") + m_systems[system].name;
            }
        }
        return MakeError(ErrorCode::InvalidArgument, "system order has a cycle involving: " + names);
    }

    // Order each conflicting pair by registration unless the graph already orders it either way. Adding i->j only
    // when j can't reach i keeps the graph acyclic.
    for (SystemId i = 0; i < count; ++i)
    {
        for (SystemId j = i + 1; j < count; ++j)
        {
            if (Conflicts(i, j) && !Reaches(i, j) && !Reaches(j, i))
            {
                m_successors[i].push_back(j);
            }
        }
    }

    m_graph.Clear();
    for (SystemId system = 0; system < count; ++system)
    {
        m_graph.AddNode(
            [this, system]
            {
                System& self = m_systems[system];
                const RunTicks ticks{.lastRun = self.lastRun, .thisRun = m_run.world->BeginRun()};
                SystemContext context(*m_run.world, *m_run.jobs, *m_run.commands, self.access, ticks, system);
                self.function(context);
                self.lastRun = ticks.thisRun;
            });
    }
    for (SystemId system = 0; system < count; ++system)
    {
        for (const SystemId next : m_successors[system])
        {
            m_graph.AddDependency(system, next);
        }
    }
    m_built = true;
    return {};
}

void Scheduler::Run(World& world, JobSystem& jobs, CommandBuffers& commands)
{
    INTERI_ASSERT(m_built, "Scheduler::Run before a successful Build");
    if (!m_built)
    {
        return;
    }
    m_run = RunState{.world = &world, .jobs = &jobs, .commands = &commands};
    m_graph.Run(jobs);
    commands.Playback(world);
}

bool Scheduler::IsOrdered(SystemId before, SystemId after) const
{
    INTERI_ASSERT(m_built);
    return before != after && Reaches(before, after);
}

} // namespace interi
