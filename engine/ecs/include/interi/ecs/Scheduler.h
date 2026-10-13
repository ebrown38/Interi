// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/Result.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/TaskGraph.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace interi
{

/// What a running system sees: the world, its change-tick window, its declared access, and command buffers for
/// structural changes (applied at the end of Scheduler::Run).
class SystemContext
{
public:
    SystemContext(World& world, JobSystem& jobs, CommandBuffers& commands, const SystemAccess& access, RunTicks ticks,
                  std::uint32_t systemIndex) noexcept
        : m_world(&world)
        , m_jobs(&jobs)
        , m_commands(&commands)
        , m_access(&access)
        , m_ticks(ticks)
        , m_systemIndex(systemIndex)
    {
    }

    [[nodiscard]] World& GetWorld() const noexcept { return *m_world; }
    [[nodiscard]] JobSystem& Jobs() const noexcept { return *m_jobs; }
    [[nodiscard]] const RunTicks& Ticks() const noexcept { return m_ticks; }
    [[nodiscard]] std::uint32_t SystemIndex() const noexcept { return m_systemIndex; }

    /// The calling thread's command buffer.
    [[nodiscard]] CommandBuffer& Commands() const noexcept { return m_commands->Local(*m_jobs); }
    /// A command sort key unique to this system and `ordinal` (e.g. a chunk ordinal), so playback order does not
    /// depend on thread scheduling.
    [[nodiscard]] std::uint64_t CommandKey(std::uint64_t ordinal) const noexcept
    {
        return (static_cast<std::uint64_t>(m_systemIndex) << 40) | ordinal;
    }

    /// Serial iteration within this system's tick window.
    template <typename F>
    void ForEachChunk(const Query& query, F&& function) const
    {
        CheckFilters(query);
        query.ForEachChunk(*m_world, m_ticks, std::forward<F>(function), m_access);
    }

    /// Parallel iteration: `function(ChunkView&, std::size_t ordinal)`, one job per thread claiming chunks.
    template <typename F>
    void ParallelForEachChunk(const Query& query, F&& function) const
    {
        CheckFilters(query);
        query.ParallelForEachChunk(*m_world, *m_jobs, m_ticks, std::forward<F>(function), m_access);
    }

private:
    void CheckFilters(const Query& query) const
    {
        INTERI_ASSERT((query.FilterReads() & ~(m_access->reads | m_access->writes)).none(),
                      "query filters on a component the system did not declare");
    }

    World* m_world;
    JobSystem* m_jobs;
    CommandBuffers* m_commands;
    const SystemAccess* m_access;
    RunTicks m_ticks;
    std::uint32_t m_systemIndex;
};

/// Runs systems in parallel where their declared component access allows it.
///
/// Two systems conflict when one writes a component the other reads or writes (a query's Changed filter must be
/// declared as a read).
/// Conflicting systems run in registration order unless an explicit RunAfter already orders them; systems without
/// a conflict or an explicit order may run at the same time. Run applies all recorded commands once every system
/// has finished.
class Scheduler
{
public:
    using SystemId = std::uint32_t;
    using SystemFunction = std::move_only_function<void(SystemContext&)>;

    SystemId AddSystem(std::string name, SystemAccess access, SystemFunction function);

    /// `system` starts only after `dependency` has finished.
    void RunAfter(SystemId system, SystemId dependency);

    /// Orders the systems. InvalidArgument if the explicit RunAfter orders form a cycle. Must succeed before Run;
    /// call again after adding systems or orders.
    Result<void> Build();

    /// Runs every system once, then plays back `commands` into the world. The calling thread helps.
    void Run(World& world, JobSystem& jobs, CommandBuffers& commands);

    /// True if `before` always finishes before `after` starts (directly or transitively). After Build.
    [[nodiscard]] bool IsOrdered(SystemId before, SystemId after) const;

    [[nodiscard]] std::size_t SystemCount() const noexcept { return m_systems.size(); }

private:
    struct System
    {
        std::string name;
        SystemAccess access;
        SystemFunction function;
        std::uint64_t lastRun = 0;
    };

    struct RunState
    {
        World* world;
        JobSystem* jobs;
        CommandBuffers* commands;
    };

    [[nodiscard]] bool Reaches(SystemId from, SystemId to) const;
    [[nodiscard]] bool Conflicts(SystemId a, SystemId b) const;

    std::vector<System> m_systems;
    std::vector<std::pair<SystemId, SystemId>> m_explicit; ///< (before, after)
    std::vector<std::vector<SystemId>> m_successors;       ///< Built graph.
    TaskGraph m_graph;
    RunState m_run{};
    bool m_built = false;
};

} // namespace interi
