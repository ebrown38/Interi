// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Device.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::rhi
{
class ResourceManager;
}

namespace interi::render
{

/// A version of a frame-graph resource. Each Write produces a new version; passes read specific versions, which is
/// how the graph knows who produces what.
template <typename Tag>
struct FrameGraphHandle
{
    static constexpr std::uint32_t kInvalid = 0xFFFF'FFFFu;
    std::uint32_t resource = kInvalid;
    std::uint32_t version = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return resource != kInvalid; }
    friend constexpr bool operator==(FrameGraphHandle, FrameGraphHandle) noexcept = default;
};

struct FrameGraphTextureTag;
struct FrameGraphBufferTag;
using FGTexture = FrameGraphHandle<FrameGraphTextureTag>;
using FGBuffer = FrameGraphHandle<FrameGraphBufferTag>;

class FrameGraph;

/// Declares a pass's resource accesses during AddPass's setup callback.
class PassBuilder
{
public:
    /// The pass reads `texture` (that version) in `state`.
    void Read(FGTexture texture, rhi::ResourceState state);
    void Read(FGBuffer buffer, rhi::ResourceState state);
    /// The pass writes `texture` in `state` (also reading it, if the state's usage does). Returns the new version,
    /// which later passes read. Only the latest version can be written.
    FGTexture Write(FGTexture texture, rhi::ResourceState state);
    FGBuffer Write(FGBuffer buffer, rhi::ResourceState state);
    /// The pass has effects outside the graph (readback, presentation): it is never culled.
    void SideEffect();

private:
    friend class FrameGraph;
    PassBuilder(FrameGraph& graph, std::uint32_t pass)
        : m_graph(graph)
        , m_pass(pass)
    {
    }

    FrameGraph& m_graph;
    std::uint32_t m_pass;
};

/// What a pass's execute callback gets: the command list and the real handles behind graph resources.
class PassContext
{
public:
    [[nodiscard]] rhi::ICommandList& Commands() const noexcept { return m_commands; }
    [[nodiscard]] rhi::IDevice& Device() const noexcept { return m_device; }
    [[nodiscard]] rhi::TextureHandle Texture(FGTexture texture) const;
    [[nodiscard]] rhi::BufferHandle Buffer(FGBuffer buffer) const;

private:
    friend class FrameGraph;
    PassContext(const FrameGraph& graph, rhi::IDevice& device, rhi::ICommandList& commands)
        : m_graph(graph)
        , m_device(device)
        , m_commands(commands)
    {
    }

    const FrameGraph& m_graph;
    rhi::IDevice& m_device;
    rhi::ICommandList& m_commands;
};

/// A declarative render graph, rebuilt every frame: Reset, declare resources and passes, Compile, Execute.
///
/// Compile culls passes whose results never reach an imported resource or a SideEffect pass, works out each
/// transient resource's lifetime, and places transient resources in one memory heap so that resources whose
/// lifetimes do not overlap share memory. Execute creates (or reuses from the previous frame, when the layout is
/// unchanged) the transient resources, and records each live pass with the barriers its accesses need.
class FrameGraph
{
public:
    /// Transient resources are destroyed through `resources`, so the GPU may still be using them.
    FrameGraph(rhi::IDevice& device, rhi::ResourceManager& resources);
    ~FrameGraph();

    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;
    FrameGraph(FrameGraph&&) = delete;
    FrameGraph& operator=(FrameGraph&&) = delete;

    /// Forgets the declared resources and passes; keeps the transient resources for reuse.
    void Reset();

    /// A transient texture: created by the graph, contents undefined until a pass writes it.
    [[nodiscard]] FGTexture CreateTexture(std::string_view name, const rhi::TextureDesc& desc);
    [[nodiscard]] FGBuffer CreateBuffer(std::string_view name, const rhi::BufferDesc& desc);
    /// An external texture, in `initial` state when the graph starts and left in `final` state when it ends.
    /// Writing an imported resource is an output of the graph.
    [[nodiscard]] FGTexture ImportTexture(std::string_view name, rhi::TextureHandle texture, rhi::ResourceState initial,
                                          rhi::ResourceState final);
    [[nodiscard]] FGBuffer ImportBuffer(std::string_view name, rhi::BufferHandle buffer, rhi::ResourceState initial,
                                        rhi::ResourceState final);

    /// Adds a pass. `setup` runs now and declares accesses through the PassBuilder; `execute` runs during
    /// Execute, unless the pass is culled.
    template <typename Setup>
    void AddPass(std::string_view name, Setup&& setup, std::function<void(PassContext&)> execute)
    {
        PassBuilder builder = BeginPass(name, std::move(execute));
        setup(builder);
    }

    /// Culls, schedules lifetimes and places transient memory. InvalidArgument when a pass reads a transient
    /// resource no earlier pass wrote.
    [[nodiscard]] Result<void> Compile();
    /// Records the live passes into `commands`.
    [[nodiscard]] Result<void> Execute(rhi::ICommandList& commands);

    [[nodiscard]] bool IsCulled(std::string_view pass) const;
    /// Bytes of the transient heap after Compile.
    [[nodiscard]] std::uint64_t TransientHeapSize() const noexcept { return m_heapSize; }
    /// Offset of a transient resource in the heap after Compile.
    [[nodiscard]] std::uint64_t TransientOffset(FGTexture texture) const;
    [[nodiscard]] std::uint64_t TransientOffset(FGBuffer buffer) const;

private:
    friend class PassBuilder;
    friend class PassContext;

    struct Resource
    {
        std::string name;
        bool isTexture = true;
        bool imported = false;
        rhi::TextureDesc textureDesc;
        rhi::BufferDesc bufferDesc;
        rhi::TextureHandle texture; ///< Imported, or placed during Execute.
        rhi::BufferHandle buffer;
        rhi::ResourceState initial = rhi::ResourceState::Undefined;
        rhi::ResourceState final = rhi::ResourceState::Undefined;
        std::uint32_t version = 0; ///< Latest version.
        // Compile results.
        std::uint32_t firstPass = 0;
        std::uint32_t lastPass = 0;
        bool used = false;
        rhi::MemoryRequirements memory;
        std::uint64_t offset = 0;
    };

    struct Access
    {
        std::uint32_t resource;
        std::uint32_t version; ///< Version read, or the version a write produces.
        rhi::ResourceState state;
        bool write;
    };

    struct Pass
    {
        std::string name;
        std::vector<Access> accesses;
        std::function<void(PassContext&)> execute;
        bool sideEffect = false;
        bool culled = false;
        bool aliasingBarrier = false; ///< Transient memory changes hands before this pass.
    };

    /// One placed resource from the previous Execute; reused while the layout is unchanged.
    struct Placed
    {
        bool isTexture;
        rhi::TextureDesc textureDesc;
        rhi::BufferDesc bufferDesc;
        std::uint64_t offset;
        rhi::TextureHandle texture;
        rhi::BufferHandle buffer;
    };

    PassBuilder BeginPass(std::string_view name, std::function<void(PassContext&)> execute);
    std::uint32_t AddResource(Resource resource);
    void AddAccess(std::uint32_t pass, std::uint32_t resource, std::uint32_t version, rhi::ResourceState state,
                   bool write);
    [[nodiscard]] std::uint32_t BumpVersion(std::uint32_t resource, std::uint32_t version);
    void Cull();
    void PlaceTransients();
    /// Creates the placed transient resources, or reuses last frame's. Returns whether they were reused.
    [[nodiscard]] Result<bool> CreateTransients();
    void ReleaseTransients();

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    std::vector<Resource> m_resourceList;
    std::vector<Pass> m_passes;
    std::uint64_t m_heapSize = 0;
    std::uint32_t m_memoryTypeBits = 0;
    bool m_compiled = false;

    rhi::HeapHandle m_heap;
    std::uint64_t m_heapCapacity = 0;
    std::uint32_t m_heapMemoryTypeBits = 0;
    std::vector<Placed> m_placed;
};

} // namespace interi::render
