// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/memory/Allocator.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace interi
{

/// Records structural changes (create, destroy, add, remove) to apply later at a sync point, so systems can request
/// them while chunks are being iterated. Each record carries a caller-chosen sort key; see CommandBuffers for how
/// keys make playback deterministic. One thread records into a buffer at a time.
class CommandBuffer
{
public:
    explicit CommandBuffer(IAllocator& allocator = GetDefaultAllocator()) noexcept
        : m_allocator(&allocator)
    {
    }
    ~CommandBuffer();

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) = delete;
    CommandBuffer& operator=(CommandBuffer&&) = delete;

    /// Creates an entity with these components at playback (each type at most once).
    template <Component... Ts>
    void CreateEntity(std::uint64_t key, Ts... components);

    /// Destroys the entity at playback. Skipped if it is already gone by then.
    void DestroyEntity(std::uint64_t key, Entity entity);

    /// Adds or assigns the component at playback. Skipped if the entity is gone by then.
    template <Component T>
    void Add(std::uint64_t key, Entity entity, T value);

    /// Removes the component at playback. Skipped if the entity is gone or lacks it.
    template <Component T>
    void Remove(std::uint64_t key, Entity entity);

    [[nodiscard]] std::size_t Size() const noexcept { return m_commands.size(); }
    [[nodiscard]] bool Empty() const noexcept { return m_commands.empty(); }

    /// Drops every record without applying it.
    void Clear() noexcept;

private:
    friend class CommandBuffers;

    enum class Op : std::uint8_t
    {
        Create,
        Destroy,
        Add,
        Remove
    };

    struct Command
    {
        std::uint64_t key;
        Entity entity;
        std::uint32_t firstComponent; ///< Into m_components (Create, Add).
        std::uint32_t componentCount;
        ComponentTypeId type; ///< Remove.
        Op op;
    };

    struct Block
    {
        std::byte* memory;
        std::size_t size;
    };

    template <Component T>
    void PushComponent(T value);
    [[nodiscard]] void* AllocatePayload(std::size_t size, std::size_t alignment);

    IAllocator* m_allocator;
    std::vector<Command> m_commands;
    std::vector<ErasedComponent> m_components; ///< data is nullptr once played back (or for tags).
    std::vector<Block> m_blocks;               ///< Payload storage; never relocated, reused after Clear.
    std::size_t m_block = 0;                   ///< Block being filled.
    std::size_t m_blockUsed = 0;
};

/// One CommandBuffer per thread that can run jobs (the workers plus one waiting thread), merged at playback.
///
/// Playback applies every record from all buffers sorted by (key, record order within its buffer). That order is
/// independent of which thread recorded what, as long as each key is only recorded on one thread at a time, e.g.
/// keys derived from a system index and the chunk ordinal of Query::ParallelForEachChunk.
class CommandBuffers
{
public:
    explicit CommandBuffers(std::size_t slotCount, IAllocator& allocator = GetDefaultAllocator());
    /// One slot per thread `jobs` can run on.
    explicit CommandBuffers(const JobSystem& jobs, IAllocator& allocator = GetDefaultAllocator())
        : CommandBuffers(jobs.ThreadCount(), allocator)
    {
    }

    /// The calling thread's buffer: worker i uses slot i, any other thread the last slot (so at most one non-worker
    /// thread may record at a time).
    [[nodiscard]] CommandBuffer& Local(const JobSystem& jobs) noexcept;
    [[nodiscard]] CommandBuffer& Slot(std::size_t index) noexcept { return *m_buffers[index]; }
    [[nodiscard]] std::size_t SlotCount() const noexcept { return m_buffers.size(); }

    /// Applies and clears every buffer. Not concurrent with recording.
    void Playback(World& world);

private:
    std::vector<std::unique_ptr<CommandBuffer>> m_buffers;
};

// --- Implementation ---------------------------------------------------------------------------------------------

template <Component T>
void CommandBuffer::PushComponent(T value)
{
    void* data = nullptr;
    if constexpr (!std::is_empty_v<T>)
    {
        data = AllocatePayload(sizeof(T), alignof(T));
        ::new (data) T(std::move(value));
    }
    m_components.push_back(ErasedComponent{.type = ComponentId<T>(), .data = data});
}

template <Component... Ts>
void CommandBuffer::CreateEntity(std::uint64_t key, Ts... components)
{
    const auto first = static_cast<std::uint32_t>(m_components.size());
    (PushComponent(std::move(components)), ...);
    m_commands.push_back(Command{.key = key,
                                 .entity = {},
                                 .firstComponent = first,
                                 .componentCount = static_cast<std::uint32_t>(sizeof...(Ts)),
                                 .type = 0,
                                 .op = Op::Create});
}

template <Component T>
void CommandBuffer::Add(std::uint64_t key, Entity entity, T value)
{
    const auto first = static_cast<std::uint32_t>(m_components.size());
    PushComponent(std::move(value));
    m_commands.push_back(
        Command{.key = key, .entity = entity, .firstComponent = first, .componentCount = 1, .type = 0, .op = Op::Add});
}

template <Component T>
void CommandBuffer::Remove(std::uint64_t key, Entity entity)
{
    m_commands.push_back(Command{.key = key,
                                 .entity = entity,
                                 .firstComponent = 0,
                                 .componentCount = 0,
                                 .type = ComponentId<T>(),
                                 .op = Op::Remove});
}

} // namespace interi
