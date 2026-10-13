// SPDX-License-Identifier: MIT
#include <interi/ecs/World.h>

#include <algorithm>

namespace interi
{

namespace
{

/// Bytes needed for `capacity` rows: the entity column, then each component column at its alignment.
[[nodiscard]] std::size_t LayoutBytes(std::vector<detail::Column>& columns, std::size_t capacity) noexcept
{
    std::size_t offset = sizeof(Entity) * capacity;
    for (detail::Column& column : columns)
    {
        if (column.size == 0)
        {
            column.offset = 0;
            continue;
        }
        offset = AlignUp(offset, column.info->alignment);
        column.offset = offset;
        offset += column.size * capacity;
    }
    return offset;
}

[[nodiscard]] std::uint64_t ChunkTick(const detail::Chunk& chunk, std::size_t column) noexcept
{
    return chunk.changedTicks[column];
}

} // namespace

World::World(IAllocator& allocator)
    : m_allocator(&allocator)
{
}

World::~World()
{
    for (const auto& archetype : m_archetypes)
    {
        for (detail::Chunk& chunk : archetype->chunks)
        {
            for (std::size_t column = 0; column < archetype->columns.size(); ++column)
            {
                const detail::Column& info = archetype->columns[column];
                if (info.size == 0)
                {
                    continue;
                }
                for (std::uint32_t row = 0; row < chunk.count; ++row)
                {
                    info.info->destroy(archetype->At(chunk, column, row));
                }
            }
            m_allocator->Free(chunk.memory, archetype->chunkBytes);
        }
    }
}

ArchetypeId World::FindOrCreateArchetype(const ComponentMask& mask)
{
    if (const auto found = m_archetypeByMask.find(mask); found != m_archetypeByMask.end())
    {
        return found->second;
    }

    auto archetype = std::make_unique<detail::Archetype>();
    archetype->mask = mask;
    archetype->columnOf.fill(detail::Archetype::kNoColumn);
    std::size_t rowBytes = sizeof(Entity);
    std::size_t alignment = alignof(std::max_align_t);
    for (ComponentTypeId type = 0; type < kMaxComponentTypes; ++type)
    {
        if (mask.test(type))
        {
            const ComponentInfo& info = ComponentInfoOf(type);
            archetype->columns.push_back(detail::Column{.type = type, .info = &info, .offset = 0, .size = info.size});
            rowBytes += info.size;
            alignment = std::max(alignment, info.alignment);
        }
    }
    // Column order must not depend on ids (first-use order), so layouts are the same in every run.
    std::ranges::sort(archetype->columns, [](const detail::Column& a, const detail::Column& b)
                      { return a.info->name != b.info->name ? a.info->name < b.info->name : a.type < b.type; });
    for (std::size_t column = 0; column < archetype->columns.size(); ++column)
    {
        archetype->columnOf[archetype->columns[column].type] = static_cast<std::int16_t>(column);
    }

    std::size_t capacity = std::max<std::size_t>(1, kChunkBytes / rowBytes);
    while (capacity > 1 && LayoutBytes(archetype->columns, capacity) > kChunkBytes)
    {
        --capacity;
    }
    archetype->capacity = static_cast<std::uint32_t>(capacity);
    archetype->chunkBytes = std::max(kChunkBytes, LayoutBytes(archetype->columns, capacity));
    archetype->chunkAlignment = alignment;

    const auto id = static_cast<ArchetypeId>(m_archetypes.size());
    m_archetypes.push_back(std::move(archetype));
    m_archetypeByMask.emplace(mask, id);
    return id;
}

ArchetypeId World::AddEdge(ArchetypeId from, ComponentTypeId type)
{
    for (const auto& [edgeType, target] : m_archetypes[from]->addEdges)
    {
        if (edgeType == type)
        {
            return target;
        }
    }
    ComponentMask mask = m_archetypes[from]->mask;
    mask.set(type);
    const ArchetypeId target = FindOrCreateArchetype(mask);
    m_archetypes[from]->addEdges.emplace_back(type, target);
    m_archetypes[target]->removeEdges.emplace_back(type, from);
    return target;
}

ArchetypeId World::RemoveEdge(ArchetypeId from, ComponentTypeId type)
{
    for (const auto& [edgeType, target] : m_archetypes[from]->removeEdges)
    {
        if (edgeType == type)
        {
            return target;
        }
    }
    ComponentMask mask = m_archetypes[from]->mask;
    mask.reset(type);
    const ArchetypeId target = FindOrCreateArchetype(mask);
    m_archetypes[from]->removeEdges.emplace_back(type, target);
    m_archetypes[target]->addEdges.emplace_back(type, from);
    return target;
}

Entity World::CreateInArchetype(ArchetypeId archetype)
{
    const RecordHandle handle = m_entities.Insert(EntityRecord{});
    const Entity entity = Entity::FromParts(handle.Index(), handle.Generation());
    *m_entities.Get(handle) = AllocateRow(archetype, entity);
    return entity;
}

World::EntityRecord World::AllocateRow(ArchetypeId archetypeId, Entity entity)
{
    detail::Archetype& archetype = *m_archetypes[archetypeId];
    const std::uint64_t tick = CurrentTick();
    if (archetype.chunks.empty() || archetype.chunks.back().count == archetype.capacity)
    {
        detail::Chunk chunk;
        chunk.memory = static_cast<std::byte*>(m_allocator->Allocate(archetype.chunkBytes, archetype.chunkAlignment));
        INTERI_VERIFY(chunk.memory != nullptr, "out of memory allocating an ECS chunk");
        chunk.changedTicks = std::make_unique<std::uint64_t[]>(archetype.columns.size());
        archetype.chunks.push_back(std::move(chunk));
    }
    detail::Chunk& chunk = archetype.chunks.back();
    const std::uint32_t row = chunk.count++;
    ::new (static_cast<void*>(archetype.Entities(chunk) + row)) Entity(entity);
    std::fill_n(chunk.changedTicks.get(), archetype.columns.size(), tick);
    return EntityRecord{
        .archetype = archetypeId, .chunk = static_cast<std::uint32_t>(archetype.chunks.size() - 1), .row = row};
}

void World::RemoveRow(ArchetypeId archetypeId, std::uint32_t chunkIndex, std::uint32_t row)
{
    detail::Archetype& archetype = *m_archetypes[archetypeId];
    const auto lastChunkIndex = static_cast<std::uint32_t>(archetype.chunks.size() - 1);
    detail::Chunk& lastChunk = archetype.chunks[lastChunkIndex];
    const std::uint32_t lastRow = lastChunk.count - 1;
    if (chunkIndex != lastChunkIndex || row != lastRow)
    {
        detail::Chunk& chunk = archetype.chunks[chunkIndex];
        for (std::size_t column = 0; column < archetype.columns.size(); ++column)
        {
            if (archetype.columns[column].size != 0)
            {
                archetype.columns[column].info->relocate(archetype.At(chunk, column, row),
                                                         archetype.At(lastChunk, column, lastRow));
            }
            // The moved row may carry newer changes than the chunk it lands in.
            chunk.changedTicks[column] = std::max(ChunkTick(chunk, column), ChunkTick(lastChunk, column));
        }
        const Entity moved = archetype.Entities(lastChunk)[lastRow];
        archetype.Entities(chunk)[row] = moved;
        EntityRecord* record = m_entities.Get(ToRecordHandle(moved));
        record->chunk = chunkIndex;
        record->row = row;
    }
    if (--lastChunk.count == 0)
    {
        m_allocator->Free(lastChunk.memory, archetype.chunkBytes);
        archetype.chunks.pop_back();
    }
}

World::EntityRecord& World::MoveToArchetype(EntityRecord& record, ArchetypeId target)
{
    const EntityRecord source = record;
    const detail::Archetype& from = *m_archetypes[source.archetype];
    const Entity entity = from.Entities(from.chunks[source.chunk])[source.row];
    const EntityRecord destination = AllocateRow(target, entity);
    const detail::Archetype& to = *m_archetypes[target];
    const detail::Chunk& fromChunk = from.chunks[source.chunk];
    const detail::Chunk& toChunk = to.chunks[destination.chunk];

    for (std::size_t column = 0; column < from.columns.size(); ++column)
    {
        const detail::Column& info = from.columns[column];
        if (info.size == 0)
        {
            continue;
        }
        void* value = from.At(fromChunk, column, source.row);
        const std::int16_t targetColumn = to.columnOf[info.type];
        if (targetColumn != detail::Archetype::kNoColumn)
        {
            info.info->relocate(to.At(toChunk, static_cast<std::size_t>(targetColumn), destination.row), value);
        }
        else
        {
            info.info->destroy(value);
        }
    }
    RemoveRow(source.archetype, source.chunk, source.row);
    record = destination;
    return record;
}

bool World::DestroyEntity(Entity entity)
{
    const RecordHandle handle = ToRecordHandle(entity);
    const EntityRecord* record = m_entities.Get(handle);
    if (record == nullptr)
    {
        return false;
    }
    const detail::Archetype& archetype = *m_archetypes[record->archetype];
    const detail::Chunk& chunk = archetype.chunks[record->chunk];
    for (std::size_t column = 0; column < archetype.columns.size(); ++column)
    {
        if (archetype.columns[column].size != 0)
        {
            archetype.columns[column].info->destroy(archetype.At(chunk, column, record->row));
        }
    }
    RemoveRow(record->archetype, record->chunk, record->row);
    m_entities.Remove(handle);
    return true;
}

Entity World::CreateErased(std::span<const ErasedComponent> components)
{
    ComponentMask mask;
    for (const ErasedComponent& component : components)
    {
        mask.set(component.type);
    }
    INTERI_ASSERT(mask.count() == components.size(), "CreateErased: each component type may appear only once");
    const Entity entity = CreateInArchetype(FindOrCreateArchetype(mask));
    const EntityRecord& record = *m_entities.Get(ToRecordHandle(entity));
    for (const ErasedComponent& component : components)
    {
        if (component.data != nullptr)
        {
            ComponentInfoOf(component.type).relocate(ComponentAt(record, component.type), component.data);
        }
    }
    return entity;
}

bool World::AddErased(Entity entity, const ErasedComponent& component)
{
    const ComponentInfo& info = ComponentInfoOf(component.type);
    EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    if (record == nullptr)
    {
        if (component.data != nullptr)
        {
            info.destroy(component.data);
        }
        return false;
    }
    if (m_archetypes[record->archetype]->mask.test(component.type))
    {
        if (component.data != nullptr)
        {
            info.moveAssign(ComponentAt(*record, component.type), component.data);
            info.destroy(component.data);
            MarkChanged(*record, component.type);
        }
        return true;
    }
    EntityRecord& moved = MoveToArchetype(*record, AddEdge(record->archetype, component.type));
    if (component.data != nullptr)
    {
        info.relocate(ComponentAt(moved, component.type), component.data);
    }
    return true;
}

bool World::RemoveErased(Entity entity, ComponentTypeId type)
{
    EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    if (record == nullptr || !m_archetypes[record->archetype]->mask.test(type))
    {
        return false;
    }
    (void)MoveToArchetype(*record, RemoveEdge(record->archetype, type));
    return true;
}

void* World::ComponentAt(const EntityRecord& record, ComponentTypeId type) const noexcept
{
    const detail::Archetype& archetype = *m_archetypes[record.archetype];
    const std::int16_t column = archetype.columnOf[type];
    INTERI_ASSERT(column != detail::Archetype::kNoColumn);
    return archetype.At(archetype.chunks[record.chunk], static_cast<std::size_t>(column), record.row);
}

void World::MarkChanged(const EntityRecord& record, ComponentTypeId type) noexcept
{
    detail::Archetype& archetype = *m_archetypes[record.archetype];
    const std::int16_t column = archetype.columnOf[type];
    INTERI_ASSERT(column != detail::Archetype::kNoColumn);
    archetype.chunks[record.chunk].changedTicks[static_cast<std::size_t>(column)] = CurrentTick();
}

ArchetypeId World::ArchetypeOf(Entity entity) const
{
    const EntityRecord* record = m_entities.Get(ToRecordHandle(entity));
    INTERI_ASSERT(record != nullptr, "ArchetypeOf: stale entity");
    return record->archetype;
}

std::size_t World::ChunkCount(ArchetypeId archetype) const
{
    INTERI_ASSERT(archetype < m_archetypes.size());
    return m_archetypes[archetype]->chunks.size();
}

} // namespace interi
