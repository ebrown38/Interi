// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace interi
{

/// Components a system declared it reads and writes. Chunk views check accesses against it when asserts are on.
struct SystemAccess
{
    ComponentMask reads;
    ComponentMask writes;

    template <Component... Ts>
    SystemAccess& Read()
    {
        (reads.set(ComponentId<Ts>()), ...);
        return *this;
    }
    template <Component... Ts>
    SystemAccess& Write()
    {
        (writes.set(ComponentId<Ts>()), ...);
        return *this;
    }
};

/// The change-tick window of one query or system run (see World): `Changed` matches writes newer than lastRun,
/// and writes during the run are stamped with thisRun.
struct RunTicks
{
    std::uint64_t lastRun = 0;
    std::uint64_t thisRun = 0;
};

/// One chunk of a query's matching rows, with typed access to its columns.
class ChunkView
{
public:
    ChunkView(detail::Archetype& archetype, detail::Chunk& chunk, std::uint64_t writeTick,
              const SystemAccess* access) noexcept
        : m_archetype(&archetype)
        , m_chunk(&chunk)
        , m_writeTick(writeTick)
        , m_access(access)
    {
    }

    [[nodiscard]] std::uint32_t Count() const noexcept { return m_chunk->count; }
    [[nodiscard]] std::span<const Entity> Entities() const noexcept
    {
        return {m_archetype->Entities(*m_chunk), m_chunk->count};
    }

    template <Component T>
    [[nodiscard]] bool Has() const
    {
        return m_archetype->mask.test(ComponentId<T>());
    }

    /// The column for T, which the archetype must have.
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] std::span<const T> Read() const
    {
        const ComponentTypeId type = ComponentId<T>();
        INTERI_ASSERT(m_access == nullptr || m_access->reads.test(type) || m_access->writes.test(type),
                      "system reads a component it did not declare");
        return {std::launder(static_cast<const T*>(ColumnData(type))), m_chunk->count};
    }

    /// The column for T, marking it changed.
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] std::span<T> Write()
    {
        const ComponentTypeId type = ComponentId<T>();
        INTERI_ASSERT(m_access == nullptr || m_access->writes.test(type),
                      "system writes a component it did not declare");
        const std::int16_t column = m_archetype->columnOf[type];
        INTERI_ASSERT(column != detail::Archetype::kNoColumn, "chunk has no such component");
        m_chunk->changedTicks[static_cast<std::size_t>(column)] = m_writeTick;
        return {std::launder(static_cast<T*>(ColumnData(type))), m_chunk->count};
    }

private:
    [[nodiscard]] void* ColumnData(ComponentTypeId type) const noexcept
    {
        const std::int16_t column = m_archetype->columnOf[type];
        INTERI_ASSERT(column != detail::Archetype::kNoColumn, "chunk has no such component");
        return m_archetype->At(*m_chunk, static_cast<std::size_t>(column), 0);
    }

    detail::Archetype* m_archetype;
    detail::Chunk* m_chunk;
    std::uint64_t m_writeTick;
    const SystemAccess* m_access;
};

/// Selects chunks by component filters: With (must have), Without (must not have), Changed (must have, and the
/// chunk's column was written after the run's lastRun tick; several Changed filters must all hold). Chunks are
/// visited in a deterministic order: archetypes in creation order, then chunk order.
///
/// Structural changes must not happen during iteration.
class Query
{
public:
    template <Component... Ts>
    Query& With()
    {
        (m_with.set(ComponentId<Ts>()), ...);
        return *this;
    }
    template <Component... Ts>
    Query& Without()
    {
        (m_without.set(ComponentId<Ts>()), ...);
        return *this;
    }
    template <Component... Ts>
    Query& Changed()
    {
        (m_changed.set(ComponentId<Ts>()), ...);
        return With<Ts...>();
    }

    /// Components whose change ticks the Changed filters read; a system using the query must declare them as reads
    /// (With/Without only test archetype membership, which can't change while systems run).
    [[nodiscard]] const ComponentMask& FilterReads() const noexcept { return m_changed; }

    /// Runs `function(ChunkView&)` on each matching chunk with this query's own change tracking: Changed matches
    /// writes since the previous call of this overload.
    template <typename F>
    void ForEachChunk(World& world, F&& function)
    {
        const RunTicks ticks{.lastRun = m_lastRun, .thisRun = world.BeginRun()};
        ForEachChunk(world, ticks, std::forward<F>(function));
        m_lastRun = ticks.thisRun;
    }

    /// Runs `function(ChunkView&)` on each matching chunk within an explicit tick window (used by systems).
    template <typename F>
    void ForEachChunk(World& world, const RunTicks& ticks, F&& function, const SystemAccess* access = nullptr) const
    {
        ForEachMatchingChunk(world, ticks.lastRun,
                             [&](detail::Archetype& archetype, detail::Chunk& chunk)
                             {
                                 ChunkView view(archetype, chunk, ticks.thisRun, access);
                                 function(view);
                             });
    }

    /// Runs `function(ChunkView&, std::size_t ordinal)` on each matching chunk in parallel. `ordinal` is the
    /// chunk's position in the deterministic visiting order, independent of thread count: use it to key command
    /// buffer records. Must be called from a thread that may wait on `jobs`.
    template <typename F>
    void ParallelForEachChunk(World& world, JobSystem& jobs, const RunTicks& ticks, F&& function,
                              const SystemAccess* access = nullptr) const
    {
        std::vector<std::pair<detail::Archetype*, detail::Chunk*>> chunks;
        ForEachMatchingChunk(world, ticks.lastRun, [&](detail::Archetype& archetype, detail::Chunk& chunk)
                             { chunks.emplace_back(&archetype, &chunk); });
        ParallelForChunks(jobs, 0, chunks.size(), 1,
                          [&](std::size_t begin, std::size_t end)
                          {
                              for (std::size_t i = begin; i < end; ++i)
                              {
                                  ChunkView view(*chunks[i].first, *chunks[i].second, ticks.thisRun, access);
                                  function(view, i);
                              }
                          });
    }

private:
    [[nodiscard]] bool MatchesArchetype(const detail::Archetype& archetype) const noexcept
    {
        return (archetype.mask & m_with) == m_with && (archetype.mask & m_without).none() && !archetype.chunks.empty();
    }

    [[nodiscard]] bool MatchesChunk(const detail::Archetype& archetype, const detail::Chunk& chunk,
                                    std::uint64_t lastRun) const noexcept
    {
        if (m_changed.none())
        {
            return true;
        }
        for (std::size_t column = 0; column < archetype.columns.size(); ++column)
        {
            if (m_changed.test(archetype.columns[column].type) && chunk.changedTicks[column] <= lastRun)
            {
                return false;
            }
        }
        return true;
    }

    template <typename F>
    void ForEachMatchingChunk(World& world, std::uint64_t lastRun, F&& function) const
    {
        for (const auto& archetype : world.Archetypes())
        {
            if (!MatchesArchetype(*archetype))
            {
                continue;
            }
            for (detail::Chunk& chunk : archetype->chunks)
            {
                if (MatchesChunk(*archetype, chunk, lastRun))
                {
                    function(*archetype, chunk);
                }
            }
        }
    }

    ComponentMask m_with;
    ComponentMask m_without;
    ComponentMask m_changed;
    std::uint64_t m_lastRun = 0;
};

} // namespace interi
