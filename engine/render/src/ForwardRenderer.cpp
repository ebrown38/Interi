// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Mat4.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/shaders/Forward.ps.h>
#include <interi/shaders/Forward.vs.h>

#include <array>
#include <cstring>

namespace interi::render
{

namespace
{

// Must match engine/render/shaders/ForwardCommon.hlsli.
struct FrameData
{
    Mat4 viewProjection;
    Vec3 cameraPosition;
    float pad0;
    Vec3 sunDirection;
    float sunIntensity;
    Vec3 sunColor;
    float pad1;
    Vec3 ambient;
    float pad2;
};
static_assert(sizeof(FrameData) == 128);

struct ObjectData
{
    Mat4 model;
    Mat4 normalMatrix;
    Vec3 baseColor;
    float metallic;
    float roughness;
    float pad[3];
};
static_assert(sizeof(ObjectData) == 160);

struct Push
{
    std::uint32_t vertices;
    std::uint32_t scene;
    std::uint32_t objectIndex;
    std::uint32_t pad;
};

constexpr std::uint64_t kSceneBufferSize = sizeof(FrameData) + sizeof(ObjectData) * ForwardRenderer::kMaxObjects;

} // namespace

ForwardRenderer::ForwardRenderer(rhi::IDevice& device, rhi::ResourceManager& resources)
    : m_device(device)
    , m_resources(resources)
{
}

Result<std::unique_ptr<ForwardRenderer>> ForwardRenderer::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                                 rhi::Format colorFormat, std::uint32_t framesInFlight)
{
    std::unique_ptr<ForwardRenderer> renderer(new ForwardRenderer(device, resources));
    const std::array formats{colorFormat};
    INTERI_TRY_ASSIGN(pipeline, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kForwardVs)),
                                                               .pixelCode = std::as_bytes(std::span(kForwardPs)),
                                                               .colorFormats = formats,
                                                               .depthFormat = rhi::Format::D32Float,
                                                               .debugName = "Forward"}));
    renderer->m_pipeline = pipeline;
    for (std::uint32_t i = 0; i < framesInFlight; ++i)
    {
        INTERI_TRY_ASSIGN(buffer, device.CreateBuffer({.size = kSceneBufferSize,
                                                       .usage = rhi::BufferUsage::Storage,
                                                       .memory = rhi::MemoryUsage::Upload,
                                                       .debugName = "Forward scene"}));
        renderer->m_sceneBuffers.push_back(buffer);
    }
    return renderer;
}

ForwardRenderer::~ForwardRenderer()
{
    if (m_pipeline.IsValid())
    {
        m_resources.DestroyPipeline(m_pipeline);
    }
    for (const rhi::BufferHandle buffer : m_sceneBuffers)
    {
        m_resources.DestroyBuffer(buffer);
    }
}

void ForwardRenderer::AddPasses(FrameGraph& graph, FGTexture& color, std::uint32_t width, std::uint32_t height,
                                const SceneView& view, std::span<const RenderObject> objects, Vec3 clearColor)
{
    INTERI_ASSERT(objects.size() <= kMaxObjects, "too many objects for the forward renderer");
    const rhi::BufferHandle sceneBuffer = m_sceneBuffers[m_nextSlot];
    m_nextSlot = (m_nextSlot + 1) % static_cast<std::uint32_t>(m_sceneBuffers.size());

    // Scene data for this frame.
    std::byte* mapped = m_device.MappedMemory(sceneBuffer).data();
    const FrameData frame{.viewProjection = view.projection * view.view,
                          .cameraPosition = view.cameraPosition,
                          .pad0 = 0,
                          .sunDirection = Normalize(view.sun.direction),
                          .sunIntensity = view.sun.intensity,
                          .sunColor = view.sun.color,
                          .pad1 = 0,
                          .ambient = view.ambient,
                          .pad2 = 0};
    std::memcpy(mapped, &frame, sizeof(frame));
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        const RenderObject& object = objects[i];
        const ObjectData data{.model = object.transform,
                              .normalMatrix = Transpose(Inverse(object.transform)),
                              .baseColor = object.material.baseColor,
                              .metallic = object.material.metallic,
                              .roughness = object.material.roughness,
                              .pad = {}};
        std::memcpy(mapped + sizeof(FrameData) + i * sizeof(ObjectData), &data, sizeof(data));
    }

    FGTexture depth = graph.CreateTexture("Forward depth", {.width = width,
                                                            .height = height,
                                                            .format = rhi::Format::D32Float,
                                                            .usage = rhi::TextureUsage::DepthTarget,
                                                            .debugName = "Forward depth"});
    const std::vector<RenderObject> drawList(objects.begin(), objects.end());
    graph.AddPass(
        "Forward",
        [&](PassBuilder& builder)
        {
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
            depth = builder.Write(depth, rhi::ResourceState::DepthWrite);
        },
        [this, color, depth, width, height, sceneBuffer, drawList, clearColor](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{
                .texture = context.Texture(color), .clearColor = {clearColor.x, clearColor.y, clearColor.z, 1.0f}}};
            const rhi::DepthAttachment depthAttachment{.texture = context.Texture(depth)};
            commands.BeginRendering({.colors = colors, .depth = &depthAttachment, .width = width, .height = height});
            commands.BindPipeline(m_pipeline);
            const std::uint32_t scene = m_device.BindlessIndex(sceneBuffer);
            for (std::uint32_t i = 0; i < drawList.size(); ++i)
            {
                const GpuMesh& mesh = *drawList[i].mesh;
                commands.PushConstants(Push{
                    .vertices = m_device.BindlessIndex(mesh.vertices), .scene = scene, .objectIndex = i, .pad = 0});
                commands.BindIndexBuffer(mesh.indices, 0, rhi::IndexFormat::Uint32);
                commands.DrawIndexed(mesh.indexCount, 1, 0, 0, 0);
            }
            commands.EndRendering();
        });
}

} // namespace interi::render
