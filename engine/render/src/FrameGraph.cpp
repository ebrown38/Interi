// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/render/FrameGraph.h>
#include <interi/rhi/ResourceManager.h>

#include <algorithm>
#include <format>
#include <utility>

namespace interi::render
{

namespace
{

[[nodiscard]] bool SameDesc(const rhi::TextureDesc& a, const rhi::TextureDesc& b)
{
    return a.width == b.width && a.height == b.height && a.mipLevels == b.mipLevels && a.arrayLayers == b.arrayLayers &&
           a.format == b.format && a.usage == b.usage;
}

[[nodiscard]] bool SameDesc(const rhi::BufferDesc& a, const rhi::BufferDesc& b)
{
    return a.size == b.size && a.usage == b.usage && a.memory == b.memory;
}

[[nodiscard]] constexpr std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

} // namespace

// --- PassBuilder / PassContext -----------------------------------------------------------------------------------

void PassBuilder::Read(FGTexture texture, rhi::ResourceState state)
{
    m_graph.AddAccess(m_pass, texture.resource, texture.version, state, false);
}

void PassBuilder::Read(FGBuffer buffer, rhi::ResourceState state)
{
    m_graph.AddAccess(m_pass, buffer.resource, buffer.version, state, false);
}

FGTexture PassBuilder::Write(FGTexture texture, rhi::ResourceState state)
{
    const std::uint32_t version = m_graph.BumpVersion(texture.resource, texture.version);
    m_graph.AddAccess(m_pass, texture.resource, version, state, true);
    return {texture.resource, version};
}

FGBuffer PassBuilder::Write(FGBuffer buffer, rhi::ResourceState state)
{
    const std::uint32_t version = m_graph.BumpVersion(buffer.resource, buffer.version);
    m_graph.AddAccess(m_pass, buffer.resource, version, state, true);
    return {buffer.resource, version};
}

void PassBuilder::SideEffect()
{
    m_graph.m_passes[m_pass].sideEffect = true;
}

rhi::TextureHandle PassContext::Texture(FGTexture texture) const
{
    INTERI_ASSERT(texture.resource < m_graph.m_resourceList.size());
    return m_graph.m_resourceList[texture.resource].texture;
}

rhi::BufferHandle PassContext::Buffer(FGBuffer buffer) const
{
    INTERI_ASSERT(buffer.resource < m_graph.m_resourceList.size());
    return m_graph.m_resourceList[buffer.resource].buffer;
}

// --- Declaration -------------------------------------------------------------------------------------------------

FrameGraph::FrameGraph(rhi::IDevice& device, rhi::ResourceManager& resources)
    : m_device(device)
    , m_resources(resources)
{
}

FrameGraph::~FrameGraph()
{
    ReleaseTransients();
}

void FrameGraph::Reset()
{
    m_resourceList.clear();
    m_passes.clear();
    m_heapSize = 0;
    m_memoryTypeBits = 0;
    m_compiled = false;
}

std::uint32_t FrameGraph::AddResource(Resource resource)
{
    m_compiled = false;
    m_resourceList.push_back(std::move(resource));
    return static_cast<std::uint32_t>(m_resourceList.size() - 1);
}

FGTexture FrameGraph::CreateTexture(std::string_view name, const rhi::TextureDesc& desc)
{
    Resource resource;
    resource.name = name;
    resource.textureDesc = desc;
    return {AddResource(std::move(resource)), 0};
}

FGBuffer FrameGraph::CreateBuffer(std::string_view name, const rhi::BufferDesc& desc)
{
    Resource resource;
    resource.name = name;
    resource.isTexture = false;
    resource.bufferDesc = desc;
    return {AddResource(std::move(resource)), 0};
}

FGTexture FrameGraph::ImportTexture(std::string_view name, rhi::TextureHandle texture, rhi::ResourceState initial,
                                    rhi::ResourceState final)
{
    Resource resource;
    resource.name = name;
    resource.imported = true;
    resource.texture = texture;
    resource.initial = initial;
    resource.final = final;
    return {AddResource(std::move(resource)), 0};
}

FGBuffer FrameGraph::ImportBuffer(std::string_view name, rhi::BufferHandle buffer, rhi::ResourceState initial,
                                  rhi::ResourceState final)
{
    Resource resource;
    resource.name = name;
    resource.isTexture = false;
    resource.imported = true;
    resource.buffer = buffer;
    resource.initial = initial;
    resource.final = final;
    return {AddResource(std::move(resource)), 0};
}

PassBuilder FrameGraph::BeginPass(std::string_view name, std::function<void(PassContext&)> execute)
{
    m_compiled = false;
    Pass pass;
    pass.name = name;
    pass.execute = std::move(execute);
    m_passes.push_back(std::move(pass));
    return {*this, static_cast<std::uint32_t>(m_passes.size() - 1)};
}

void FrameGraph::AddAccess(std::uint32_t pass, std::uint32_t resource, std::uint32_t version, rhi::ResourceState state,
                           bool write)
{
    INTERI_ASSERT(resource < m_resourceList.size(), "invalid frame graph handle");
    m_passes[pass].accesses.push_back(Access{.resource = resource, .version = version, .state = state, .write = write});
}

std::uint32_t FrameGraph::BumpVersion(std::uint32_t resource, std::uint32_t version)
{
    INTERI_ASSERT(resource < m_resourceList.size(), "invalid frame graph handle");
    Resource& entry = m_resourceList[resource];
    INTERI_ASSERT(version == entry.version, "only the latest version of a resource can be written");
    return ++entry.version;
}

// --- Compile -----------------------------------------------------------------------------------------------------

Result<void> FrameGraph::Compile()
{
    for (const Pass& pass : m_passes)
    {
        for (const Access& access : pass.accesses)
        {
            const Resource& resource = m_resourceList[access.resource];
            if (!access.write && !resource.imported && access.version == 0)
            {
                return MakeError(
                    ErrorCode::InvalidArgument,
                    std::format("pass '{}' reads transient '{}' before any pass writes it", pass.name, resource.name));
            }
        }
    }
    Cull();

    for (Resource& resource : m_resourceList)
    {
        resource.used = false;
    }
    for (std::uint32_t p = 0; p < m_passes.size(); ++p)
    {
        if (m_passes[p].culled)
        {
            continue;
        }
        for (const Access& access : m_passes[p].accesses)
        {
            Resource& resource = m_resourceList[access.resource];
            if (!resource.used)
            {
                resource.firstPass = p;
                resource.used = true;
            }
            resource.lastPass = p;
        }
    }
    PlaceTransients();
    m_compiled = true;
    return {};
}

void FrameGraph::Cull()
{
    // Walk backwards from the outputs (side effects and writes to imported resources): a pass is live if a live pass
    // reads a version it produced, or writes a later version of a resource it wrote (read-modify-write).
    std::vector<std::vector<std::int64_t>> producers(m_resourceList.size());
    for (std::size_t r = 0; r < m_resourceList.size(); ++r)
    {
        producers[r].assign(m_resourceList[r].version + 1, -1);
    }
    for (std::uint32_t p = 0; p < m_passes.size(); ++p)
    {
        for (const Access& access : m_passes[p].accesses)
        {
            if (access.write)
            {
                producers[access.resource][access.version] = p;
            }
        }
    }

    std::vector<std::uint32_t> worklist;
    for (std::uint32_t p = 0; p < m_passes.size(); ++p)
    {
        Pass& pass = m_passes[p];
        const bool output = pass.sideEffect ||
                            std::ranges::any_of(pass.accesses, [this](const Access& access)
                                                { return access.write && m_resourceList[access.resource].imported; });
        pass.culled = !output;
        if (output)
        {
            worklist.push_back(p);
        }
    }
    while (!worklist.empty())
    {
        const std::uint32_t p = worklist.back();
        worklist.pop_back();
        for (const Access& access : m_passes[p].accesses)
        {
            // A read depends on the version read; a write on the version it replaces.
            const std::uint32_t needed = access.write ? access.version - 1 : access.version;
            const std::int64_t producer = producers[access.resource][needed];
            if (producer >= 0 && m_passes[static_cast<std::size_t>(producer)].culled)
            {
                m_passes[static_cast<std::size_t>(producer)].culled = false;
                worklist.push_back(static_cast<std::uint32_t>(producer));
            }
        }
    }
}

void FrameGraph::PlaceTransients()
{
    // First fit over memory, among resources whose lifetimes overlap, in order of first use.
    std::vector<std::uint32_t> order;
    for (std::uint32_t r = 0; r < m_resourceList.size(); ++r)
    {
        Resource& resource = m_resourceList[r];
        if (resource.imported || !resource.used)
        {
            continue;
        }
        resource.memory = resource.isTexture ? m_device.GetTextureMemoryRequirements(resource.textureDesc)
                                             : m_device.GetBufferMemoryRequirements(resource.bufferDesc);
        order.push_back(r);
    }
    std::ranges::stable_sort(order, {}, [this](std::uint32_t r) { return m_resourceList[r].firstPass; });

    m_heapSize = 0;
    m_memoryTypeBits = ~0u;
    std::vector<std::uint32_t> placed;
    for (const std::uint32_t r : order)
    {
        Resource& resource = m_resourceList[r];
        // Ranges of already-placed resources that are alive at the same time, sorted by offset.
        std::vector<std::pair<std::uint64_t, std::uint64_t>> busy;
        for (const std::uint32_t other : placed)
        {
            const Resource& o = m_resourceList[other];
            if (o.firstPass <= resource.lastPass && resource.firstPass <= o.lastPass)
            {
                busy.emplace_back(o.offset, o.offset + o.memory.size);
            }
        }
        std::ranges::sort(busy);
        std::uint64_t offset = 0;
        for (const auto& [begin, end] : busy)
        {
            offset = AlignUp(offset, resource.memory.alignment);
            if (offset + resource.memory.size <= begin)
            {
                break;
            }
            offset = std::max(offset, end);
        }
        resource.offset = AlignUp(offset, resource.memory.alignment);
        m_heapSize = std::max(m_heapSize, resource.offset + resource.memory.size);
        m_memoryTypeBits &= resource.memory.memoryTypeBits;

        // Taking over memory a finished resource used needs an aliasing barrier before the first use.
        for (const std::uint32_t other : placed)
        {
            const Resource& o = m_resourceList[other];
            const bool memoryOverlaps =
                o.offset < resource.offset + resource.memory.size && resource.offset < o.offset + o.memory.size;
            if (memoryOverlaps && o.lastPass < resource.firstPass)
            {
                m_passes[resource.firstPass].aliasingBarrier = true;
            }
        }
        placed.push_back(r);
    }
    if (placed.empty())
    {
        m_memoryTypeBits = 0;
    }
}

// --- Execute -----------------------------------------------------------------------------------------------------

void FrameGraph::ReleaseTransients()
{
    for (const Placed& placed : m_placed)
    {
        if (placed.isTexture)
        {
            m_resources.DestroyTexture(placed.texture);
        }
        else
        {
            m_resources.DestroyBuffer(placed.buffer);
        }
    }
    m_placed.clear();
    if (m_heap.IsValid())
    {
        m_resources.DestroyHeap(m_heap);
        m_heap = {};
    }
    m_heapCapacity = 0;
    m_heapMemoryTypeBits = 0;
}

Result<bool> FrameGraph::CreateTransients()
{
    std::vector<std::uint32_t> transients;
    for (std::uint32_t r = 0; r < m_resourceList.size(); ++r)
    {
        if (!m_resourceList[r].imported && m_resourceList[r].used)
        {
            transients.push_back(r);
        }
    }

    // Reuse last frame's resources when every transient matches one in place (same description and offset).
    const auto matches = [](const Resource& resource, const Placed& placed)
    {
        return placed.isTexture == resource.isTexture && placed.offset == resource.offset &&
               (resource.isTexture ? SameDesc(placed.textureDesc, resource.textureDesc)
                                   : SameDesc(placed.bufferDesc, resource.bufferDesc));
    };
    const bool reusable =
        m_heap.IsValid() && m_heapCapacity >= m_heapSize &&
        (m_heapMemoryTypeBits & m_memoryTypeBits) == m_heapMemoryTypeBits && transients.size() == m_placed.size() &&
        std::ranges::all_of(transients,
                            [&](std::uint32_t r)
                            {
                                return std::ranges::any_of(m_placed, [&](const Placed& placed)
                                                           { return matches(m_resourceList[r], placed); });
                            });
    if (!reusable)
    {
        ReleaseTransients();
        if (transients.empty())
        {
            return false;
        }
        INTERI_TRY_ASSIGN(heap,
                          m_device.CreateHeap(rhi::HeapDesc{.size = m_heapSize, .memoryTypeBits = m_memoryTypeBits}));
        m_heap = heap;
        m_heapCapacity = m_heapSize;
        m_heapMemoryTypeBits = m_memoryTypeBits;
        for (const std::uint32_t r : transients)
        {
            const Resource& resource = m_resourceList[r];
            Placed placed{.isTexture = resource.isTexture,
                          .textureDesc = resource.textureDesc,
                          .bufferDesc = resource.bufferDesc,
                          .offset = resource.offset,
                          .texture = {},
                          .buffer = {}};
            if (resource.isTexture)
            {
                INTERI_TRY_ASSIGN(texture, m_device.CreatePlacedTexture(resource.textureDesc, m_heap, resource.offset));
                placed.texture = texture;
            }
            else
            {
                INTERI_TRY_ASSIGN(buffer, m_device.CreatePlacedBuffer(resource.bufferDesc, m_heap, resource.offset));
                placed.buffer = buffer;
            }
            m_placed.push_back(placed);
        }
    }

    // Hand each transient its placed resource (each placement is used once per frame).
    std::vector<bool> taken(m_placed.size(), false);
    for (const std::uint32_t r : transients)
    {
        Resource& resource = m_resourceList[r];
        for (std::size_t i = 0; i < m_placed.size(); ++i)
        {
            if (!taken[i] && matches(resource, m_placed[i]))
            {
                taken[i] = true;
                resource.texture = m_placed[i].texture;
                resource.buffer = m_placed[i].buffer;
                break;
            }
        }
    }
    return reusable;
}

Result<void> FrameGraph::Execute(rhi::ICommandList& commands)
{
    INTERI_ASSERT(m_compiled, "Compile the frame graph before executing it");
    INTERI_TRY_ASSIGN(reused, CreateTransients());
    if (reused)
    {
        // Earlier frames may still be using this memory on the GPU; their last accesses are unknown here, so order
        // everything submitted before against this frame.
        commands.AliasingBarrier();
    }

    std::vector<rhi::ResourceState> states(m_resourceList.size());
    for (std::size_t r = 0; r < m_resourceList.size(); ++r)
    {
        states[r] = m_resourceList[r].imported ? m_resourceList[r].initial : rhi::ResourceState::Undefined;
    }

    std::vector<rhi::BufferBarrier> bufferBarriers;
    std::vector<rhi::TextureBarrier> textureBarriers;
    const auto transition = [&](std::uint32_t r, rhi::ResourceState next)
    {
        const rhi::ResourceState current = states[r];
        // Shader writes need a barrier even when the state does not change (write-after-write).
        if (current == next && next != rhi::ResourceState::ShaderWrite)
        {
            return;
        }
        const Resource& resource = m_resourceList[r];
        if (resource.isTexture)
        {
            textureBarriers.push_back({resource.texture, current, next});
        }
        else
        {
            bufferBarriers.push_back({resource.buffer, current, next});
        }
        states[r] = next;
    };
    const auto flush = [&]
    {
        if (!bufferBarriers.empty() || !textureBarriers.empty())
        {
            commands.Barriers(bufferBarriers, textureBarriers);
            bufferBarriers.clear();
            textureBarriers.clear();
        }
    };

    for (Pass& pass : m_passes)
    {
        if (pass.culled)
        {
            continue;
        }
        if (pass.aliasingBarrier)
        {
            commands.AliasingBarrier();
        }
        for (const Access& access : pass.accesses)
        {
            transition(access.resource, access.state);
        }
        flush();
        PassContext context(*this, m_device, commands);
        if (pass.execute)
        {
            pass.execute(context);
        }
    }

    for (std::uint32_t r = 0; r < m_resourceList.size(); ++r)
    {
        const Resource& resource = m_resourceList[r];
        if (resource.imported && states[r] != resource.final)
        {
            transition(r, resource.final);
        }
    }
    flush();
    return {};
}

// --- Queries -----------------------------------------------------------------------------------------------------

bool FrameGraph::IsCulled(std::string_view pass) const
{
    const auto found = std::ranges::find_if(m_passes, [pass](const Pass& entry) { return entry.name == pass; });
    INTERI_ASSERT(found != m_passes.end(), "no such pass");
    return found != m_passes.end() && found->culled;
}

std::uint64_t FrameGraph::TransientOffset(FGTexture texture) const
{
    return m_resourceList[texture.resource].offset;
}

std::uint64_t FrameGraph::TransientOffset(FGBuffer buffer) const
{
    return m_resourceList[buffer.resource].offset;
}

} // namespace interi::render
