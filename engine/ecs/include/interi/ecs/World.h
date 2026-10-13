// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/containers/SlotMap.h>
#include <interi/core/memory/Allocator.h>
#include <interi/ecs/Component.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace interi
{

struct EntityTag
{
};

/// Generational entity handle. Stale handles (destroyed entities) are detected, never reused.
using Entity = Handle<EntityTag>;

/// Index of an archetype in its World, in creation order.
using ArchetypeId = std::uint32_t;

/// Target size of one chunk of rows. A row too large for it gets a chunk of one row.
inline constexpr std::size_t kChunkBytes = std::size_t{16} * 1024;

namespace detail
{

struct Column
{
    ComponentTypeId type;
    const ComponentInfo* info;
    std::size_t offset; ///< Byte offset of the column in a chunk.
    std::size_t size;   ///< Element size, 0 for tags.
};

struct Chunk
{
    std::byte* memory = nullptr;
    std::uint32_t count = 0;
    /// Per column: the tick of the latest write to any row (see World ticks).
    std::unique_ptr<std::uint64_t[]> changedTicks;
};

/// The entities that have exactly one set of components, stored as chunks of SoA columns. Rows are packed: every
/// chunk but the last is full.
struct Archetype
{
    static constexpr std::int16_t kNoColumn = -1;

    ComponentMask mask;
    std::vector<Column> columns;                             ///< Layout order: by type name, then id.
    std::array<std::int16_t, kMaxComponentTypes> columnOf{}; ///< Column index per component type, or kNoColumn.
    std::uint32_t capacity = 0;                              ///< Rows per chunk.
    std::size_t chunkBytes = 0;
    std::size_t chunkAlignment = 0;
    std::vector<Chunk> chunks;
    std::vector<std::pair<ComponentTypeId, ArchetypeId>> addEdges;
    std::vector<std::pair<ComponentTypeId, ArchetypeId>> removeEdges;

    [[nodiscard]] Entity* Entities(const Chunk& chunk) const noexcept
    {
        return std::launder(reinterpret_cast<Entity*>(chunk.memory));
    }
    [[nodiscard]] void* At(const Chunk& chunk, std::size_t column, std::uint32_t row) const noexcept
    {
        return chunk.memory + columns[column].offset + columns[column].size * row;
    }
};

} // namespace detail

/// Archetype-based entity storage. Each distinct component set is an archetype holding 16 KiB chunks of SoA
/// columns; adding or removing a component moves the entity's row to another archetype (edges are cached).
///
/// Change ticks: the world tick starts at 1. A system or query run takes a run tick with BeginRun() (each run gets
/// a distinct, increasing tick) and stamps chunk columns it writes with it. Direct writes through the World stamp
/// CurrentTick(), which is above every run tick handed out so far. Any row written into a chunk (create, structural
/// move, swap-remove fill) stamps that chunk's columns, so structural changes count as changes.
///
/// Not thread-safe for structural changes (create/destroy/add/remove); defer those through command buffers while
/// systems run. Chunk memory comes from the allocator passed at construction.
class World
{
public:
    explicit World(IAllocator& allocator = GetDefaultAllocator());
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    /// Creates an entity with the given components (each type at most once).
    template <Component... Ts>
    Entity CreateEntity(Ts... components);

    /// Destroys the entity and its components. False for stale handles.
    bool DestroyEntity(Entity entity);

    [[nodiscard]] bool IsAlive(Entity entity) const noexcept { return m_entities.Contains(ToRecordHandle(entity)); }

    /// Adds the component, or assigns it if the entity already has one. False for stale handles.
    template <Component T>
    bool Add(Entity entity, T value);

    /// Removes the component. False if the entity is stale or doesn't have it.
    template <Component T>
    bool Remove(Entity entity);

    template <Component T>
    [[nodiscard]] bool Has(Entity entity) const noexcept;

    /// The component, or nullptr if the entity is stale or doesn't have it. Invalidated by structural changes.
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] const T* Get(Entity entity) const noexcept;

    /// Like Get, and marks the component's chunk column as changed.
    template <Component T>
        requires(!std::is_empty_v<T>)
    [[nodiscard]] T* GetMut(Entity entity) noexcept;

    /// Type-erased CreateEntity: relocates each component out of its `data` (which is left destroyed).
    Entity CreateErased(std::span<const ErasedComponent> components);
    /// Type-erased Add: relocates or move-assigns out of `component.data`, which is destroyed either way (also when
    /// the entity is stale, which returns false).
    bool AddErased(Entity entity, const ErasedComponent& component);
    /// Type-erased Remove.
    bool RemoveErased(Entity entity, ComponentTypeId type);

    [[nodiscard]] std::size_t EntityCount() const noexcept { return m_entities.Size(); }
    [[nodiscard]] std::size_t ArchetypeCount() const noexcept { return m_archetypes.size(); }
    [[nodiscard]] ArchetypeId ArchetypeOf(Entity entity) const;
    [[nodiscard]] std::size_t ChunkCount(ArchetypeId archetype) const;

    [[nodiscard]] std::uint64_t CurrentTick() const noexcept { return m_tick.load(std::memory_order_relaxed); }
    /// Hands out a new run tick. Thread-safe.
    [[nodiscard]] std::uint64_t BeginRun() noexcept { return m_tick.fetch_add(1, std::memory_order_relaxed); }

    /// Archetypes in creation order (for queries).
    [[nodiscard]] const std::vector<std::unique_ptr<detail::Archetype>>& Archetypes() const noexcept
    {
        return m_archetypes;
    }

private:
    struct EntityRecord
    {
        ArchetypeId archetype;
        std::uint32_t chunk;
        std::uint32_t row;
    };

    using RecordHandle = Handle<EntityRecord>;

    [[nodiscard]] static RecordHandle ToRecordHandle(Entity entity) noexcept
    {
        return RecordHandle::FromParts(entity.Index(), entity.Generation());
    }

    [[nodiscard]] ArchetypeId FindOrCreateArchetype(const ComponentMask& mask);
    [[nodiscard]] ArchetypeId AddEdge(ArchetypeId from, ComponentTypeId type);
    [[nodiscard]] ArchetypeId RemoveEdge(ArchetypeId from, ComponentTypeId type);

    /// Creates an entity with an uninitialised row in `archetype`; the caller constructs every column.
    [[nodiscard]] Entity CreateInArchetype(ArchetypeId archetype);
    /// Moves the entity's row to `target`: shared columns are relocated, columns only in the source destroyed,
    /// columns only in the target left uninitialised. Returns the entity's updated record.
    EntityRecord& MoveToArchetype(EntityRecord& record, ArchetypeId target);
    /// Appends an uninitialised row for `entity`.
    [[nodiscard]] EntityRecord AllocateRow(ArchetypeId archetype, Entity entity);
    /// Fills the (already vacated) row with the archetype's last row and shrinks the archetype.
    void RemoveRow(ArchetypeId archetype, std::uint32_t chunk, std::uint32_t row);
    [[nodiscard]] void* ComponentAt(const EntityRecord& record, ComponentTypeId type) const noexcept;
    void MarkChanged(const EntityRecord& record, ComponentTypeId type) noexcept;

    IAllocator* m_allocator;
    SlotMap<EntityRecord> m_entities;
    std::vector<std::unique_ptr<detail::Archetype>> m_archetypes;
    std::unordered_map<ComponentMask, ArchetypeId> m_archetypeByMask;
    std::atomic<std::uint64_t> m_tick{1};
};

// --- Implementation ---------------------------------------------------------------------------------------------

template <Component... Ts>
Entity World::CreateEntity(Ts... components)
{
    ComponentMask mask;
    (mask.set(ComponentId<Ts>()), ...);
    INTERI_ASSERT(mask.count() == sizeof...(Ts), "CreateEntity: each component type may appear only once");
    const Entity entity = CreateInArchetype(FindOrCreateArchetype(mask));
    const EntityRecord& record = *m_entities.Get(ToRecordHandle(entity));
    (
        [&]
        {
            if constexpr (!std::is_empty_v<Ts>)
            {
                ::new (ComponentAt(record, ComponentId<Ts>())) Ts(std::move(components));
            }
        }(),
        ...);
    return entity;
}

template <Component T>
bool World::Add(Entity entity, T value)
{
    EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    if (record == nullptr)
    {
        return false;
    }
    const ComponentTypeId type = ComponentId<T>();
    if (m_archetypes[record->archetype]->mask.test(type))
    {
        if constexpr (!std::is_empty_v<T>)
        {
            *std::launder(static_cast<T*>(ComponentAt(*record, type))) = std::move(value);
            MarkChanged(*record, type);
        }
        return true;
    }
    EntityRecord& moved = MoveToArchetype(*record, AddEdge(record->archetype, type));
    if constexpr (!std::is_empty_v<T>)
    {
        ::new (ComponentAt(moved, type)) T(std::move(value));
    }
    return true;
}

template <Component T>
bool World::Remove(Entity entity)
{
    EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    const ComponentTypeId type = ComponentId<T>();
    if (record == nullptr || !m_archetypes[record->archetype]->mask.test(type))
    {
        return false;
    }
    (void)MoveToArchetype(*record, RemoveEdge(record->archetype, type));
    return true;
}

template <Component T>
bool World::Has(Entity entity) const noexcept
{
    const EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    return record != nullptr && m_archetypes[record->archetype]->mask.test(ComponentId<T>());
}

template <Component T>
    requires(!std::is_empty_v<T>)
const T* World::Get(Entity entity) const noexcept
{
    const EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    const ComponentTypeId type = ComponentId<T>();
    if (record == nullptr || !m_archetypes[record->archetype]->mask.test(type))
    {
        return nullptr;
    }
    return std::launder(static_cast<const T*>(ComponentAt(*record, type)));
}

template <Component T>
    requires(!std::is_empty_v<T>)
T* World::GetMut(Entity entity) noexcept
{
    const EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    const ComponentTypeId type = ComponentId<T>();
    if (record == nullptr || !m_archetypes[record->archetype]->mask.test(type))
    {
        return nullptr;
    }
    MarkChanged(*record, type);
    return std::launder(static_cast<T*>(ComponentAt(*record, type)));
}

} // namespace interi
