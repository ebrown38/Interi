// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/ecs/CommandBuffer.h>

#include <algorithm>
#include <span>
#include <tuple>

namespace interi
{

namespace
{

constexpr std::size_t kPayloadBlockBytes = std::size_t{16} * 1024;
constexpr std::size_t kPayloadBlockAlignment = 64;

} // namespace

CommandBuffer::~CommandBuffer()
{
    Clear();
    for (const Block& block : m_blocks)
    {
        m_allocator->Free(block.memory, block.size);
    }
}

void CommandBuffer::DestroyEntity(std::uint64_t key, Entity entity)
{
    m_commands.push_back(
        Command{.key = key, .entity = entity, .firstComponent = 0, .componentCount = 0, .type = 0, .op = Op::Destroy});
}

void* CommandBuffer::AllocatePayload(std::size_t size, std::size_t alignment)
{
    INTERI_ASSERT(alignment <= kPayloadBlockAlignment, "over-aligned component");
    while (m_block < m_blocks.size())
    {
        const std::size_t offset = AlignUp(m_blockUsed, alignment);
        if (offset + size <= m_blocks[m_block].size)
        {
            m_blockUsed = offset + size;
            return m_blocks[m_block].memory + offset;
        }
        ++m_block;
        m_blockUsed = 0;
    }
    const std::size_t blockSize = std::max(kPayloadBlockBytes, AlignUp(size, kPayloadBlockAlignment));
    auto* memory = static_cast<std::byte*>(m_allocator->Allocate(blockSize, kPayloadBlockAlignment));
    INTERI_VERIFY(memory != nullptr, "out of memory allocating a command buffer block");
    m_blocks.push_back(Block{.memory = memory, .size = blockSize});
    m_block = m_blocks.size() - 1;
    m_blockUsed = size;
    return memory;
}

void CommandBuffer::Clear() noexcept
{
    for (const ErasedComponent& component : m_components)
    {
        if (component.data != nullptr)
        {
            DestroyComponent(ComponentInfoOf(component.type), component.data);
        }
    }
    m_commands.clear();
    m_components.clear();
    m_block = 0;
    m_blockUsed = 0;
}

CommandBuffers::CommandBuffers(std::size_t slotCount, IAllocator& allocator)
{
    INTERI_ASSERT(slotCount > 0);
    m_buffers.reserve(slotCount);
    for (std::size_t i = 0; i < slotCount; ++i)
    {
        m_buffers.push_back(std::make_unique<CommandBuffer>(allocator));
    }
}

CommandBuffer& CommandBuffers::Local(const JobSystem& jobs) noexcept
{
    const std::optional<std::uint32_t> worker = jobs.CurrentWorkerIndex();
    if (worker.has_value() && *worker + 1 < m_buffers.size())
    {
        return *m_buffers[*worker];
    }
    return *m_buffers.back();
}

void CommandBuffers::Playback(World& world)
{
    struct Entry
    {
        std::uint64_t key;
        std::uint32_t sequence;
        std::uint32_t slot;
    };
    std::vector<Entry> entries;
    for (std::size_t slot = 0; slot < m_buffers.size(); ++slot)
    {
        const auto& commands = m_buffers[slot]->m_commands;
        for (std::size_t i = 0; i < commands.size(); ++i)
        {
            entries.push_back(Entry{.key = commands[i].key,
                                    .sequence = static_cast<std::uint32_t>(i),
                                    .slot = static_cast<std::uint32_t>(slot)});
        }
    }
    std::ranges::sort(entries, [](const Entry& a, const Entry& b)
                      { return std::tie(a.key, a.sequence, a.slot) < std::tie(b.key, b.sequence, b.slot); });

    for (const Entry& entry : entries)
    {
        CommandBuffer& buffer = *m_buffers[entry.slot];
        const CommandBuffer::Command& command = buffer.m_commands[entry.sequence];
        const std::span<ErasedComponent> components(buffer.m_components.data() + command.firstComponent,
                                                    command.componentCount);
        switch (command.op)
        {
        case CommandBuffer::Op::Create:
            (void)world.CreateErased(components);
            break;
        case CommandBuffer::Op::Destroy:
            (void)world.DestroyEntity(command.entity);
            break;
        case CommandBuffer::Op::Add:
            (void)world.AddErased(command.entity, components.front());
            break;
        case CommandBuffer::Op::Remove:
            (void)world.RemoveErased(command.entity, command.type);
            break;
        }
        for (ErasedComponent& component : components)
        {
            component.data = nullptr; // consumed (relocated into the world, or destroyed by it)
        }
    }
    for (const auto& buffer : m_buffers)
    {
        buffer->Clear();
    }
}

} // namespace interi
