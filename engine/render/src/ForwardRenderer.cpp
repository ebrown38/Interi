// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/Shadows.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/shaders/Cull.cs.h>
#include <interi/shaders/Forward.ps.h>
#include <interi/shaders/Forward.vs.h>
#include <interi/shaders/LightCulling.cs.h>
#include <interi/shaders/Shadow.ps.h>
#include <interi/shaders/Shadow.vs.h>

#include <array>
#include <cmath>
#include <cstring>

namespace interi::render
{

namespace
{

// Must match engine/render/shaders/ForwardCommon.hlsli.
struct FrameData
{
    Mat4 viewProjection;
    Mat4 view;
    Vec3 cameraPosition;
    float pad0;
    Vec3 sunDirection;
    float sunIntensity;
    Vec3 sunColor;
    float pad1;
    Vec3 ambient;
    float pad2;
    std::array<Mat4, kMaxShadowCascades> cascades;
    std::array<float, kMaxShadowCascades> cascadeSplits;
    std::array<float, kMaxShadowCascades> cascadeTexels;
    std::uint32_t lightCount;
    std::uint32_t lightsOffset;
    float screenWidth;
    float screenHeight;
    float clusterNear;
    float clusterFar;
    float tanHalfFovY;
    float aspect;
    std::uint32_t cascadeCount;
    std::uint32_t pad3[3];
};
static_assert(sizeof(FrameData) == 528);

struct ObjectData
{
    Mat4 model;
    Mat4 normalMatrix;
    Vec3 baseColor;
    float metallic;
    float roughness;
    float pad[3];
    Vec3 boundsCenter;
    float boundsRadius;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
    std::int32_t vertexOffset;
    std::uint32_t flags;
};
static_assert(sizeof(ObjectData) == 192);
constexpr std::uint32_t kCastsShadow = 1;

struct ForwardPush
{
    std::uint32_t vertices;
    std::uint32_t scene;
    std::uint32_t pad0;
    std::uint32_t clusters;
    std::uint32_t shadowMap;
    std::uint32_t shadowSampler;
    std::uint32_t pad[2];
};

struct ShadowPush
{
    std::uint32_t vertices;
    std::uint32_t scene;
    std::uint32_t cascade;
    std::uint32_t pad;
};

struct CullPush
{
    std::uint32_t scene;
    std::uint32_t objectCount;
    std::uint32_t viewMatrixOffset;
    std::uint32_t shadowView;
    std::uint32_t arguments;
    std::uint32_t argumentsOffset;
    std::uint32_t count;
    std::uint32_t countOffset;
};

constexpr std::uint32_t kDrawCommandSize = 20; // DrawIndexedIndirect: 5 x uint32
constexpr std::uint32_t kMaxViews = 1 + kMaxShadowCascades;
constexpr std::uint32_t kFrameCascadesOffset = 192; // FrameData::cascades

struct CullingPush
{
    std::uint32_t scene;
    std::uint32_t clusters;
};

constexpr std::uint64_t kLightsOffset = sizeof(FrameData) + sizeof(ObjectData) * ForwardRenderer::kMaxObjects;
constexpr std::uint64_t kSceneBufferSize = kLightsOffset + sizeof(PointLight) * ForwardRenderer::kMaxPointLights;

} // namespace

ForwardRenderer::ForwardRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, const MeshPool& meshes,
                                 const ForwardSettings& settings)
    : m_device(device)
    , m_resources(resources)
    , m_meshes(meshes)
    , m_settings(settings)
{
}

Result<std::unique_ptr<ForwardRenderer>> ForwardRenderer::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                                 const MeshPool& meshes, rhi::Format colorFormat,
                                                                 const ForwardSettings& settings)
{
    INTERI_ASSERT(settings.framesInFlight > 0 && settings.cascadeCount <= kMaxShadowCascades);
    std::unique_ptr<ForwardRenderer> renderer(new ForwardRenderer(device, resources, meshes, settings));
    const std::array formats{colorFormat};
    INTERI_TRY_ASSIGN(forward, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kForwardVs)),
                                                              .pixelCode = std::as_bytes(std::span(kForwardPs)),
                                                              .colorFormats = formats,
                                                              .depthFormat = rhi::Format::D32Float,
                                                              .debugName = "Forward"}));
    renderer->m_forward = forward;
    // Reverse-Z: pushing shadow-map depth away from the light means making it smaller.
    INTERI_TRY_ASSIGN(shadow, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kShadowVs)),
                                                             .pixelCode = std::as_bytes(std::span(kShadowPs)),
                                                             .depthFormat = rhi::Format::D32Float,
                                                             .cullMode = rhi::CullMode::None,
                                                             .depthBiasConstant = -1.0f,
                                                             .depthBiasSlope = -1.5f,
                                                             .debugName = "Shadow"}));
    renderer->m_shadow = shadow;
    INTERI_TRY_ASSIGN(culling, device.CreateComputePipeline(
                                   {.code = std::as_bytes(std::span(kLightCullingCs)), .debugName = "LightCulling"}));
    renderer->m_lightCulling = culling;
    INTERI_TRY_ASSIGN(cull,
                      device.CreateComputePipeline({.code = std::as_bytes(std::span(kCullCs)), .debugName = "Cull"}));
    renderer->m_cull = cull;
    INTERI_TRY_ASSIGN(sampler, device.CreateSampler({.filter = rhi::Filter::Linear,
                                                     .address = rhi::AddressMode::ClampToEdge,
                                                     .compare = true,
                                                     .compareOp = rhi::CompareOp::GreaterEqual}));
    renderer->m_shadowSampler = sampler;
    for (std::uint32_t i = 0; i < settings.framesInFlight; ++i)
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
    for (const rhi::PipelineHandle pipeline : {m_forward, m_shadow, m_lightCulling, m_cull})
    {
        if (pipeline.IsValid())
        {
            m_resources.DestroyPipeline(pipeline);
        }
    }
    if (m_shadowSampler.IsValid())
    {
        m_resources.DestroySampler(m_shadowSampler);
    }
    for (const rhi::BufferHandle buffer : m_sceneBuffers)
    {
        m_resources.DestroyBuffer(buffer);
    }
}

ForwardOutputs ForwardRenderer::AddPasses(FrameGraph& graph, FGTexture& color, std::uint32_t width,
                                          std::uint32_t height, const SceneView& view,
                                          std::span<const RenderObject> objects, Vec3 clearColor)
{
    INTERI_ASSERT(objects.size() <= kMaxObjects, "too many objects for the forward renderer");
    INTERI_ASSERT(view.pointLights.size() <= kMaxPointLights, "too many point lights for the forward renderer");
    const rhi::BufferHandle sceneBuffer = m_sceneBuffers[m_nextSlot];
    m_nextSlot = (m_nextSlot + 1) % static_cast<std::uint32_t>(m_sceneBuffers.size());

    // Scene data for this frame.
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 projection = PerspectiveReverseZ(view.verticalFov, aspect, view.nearPlane);
    const Vec3 sunDirection = Normalize(view.sun.direction);
    FrameData frame{};
    frame.viewProjection = projection * view.view;
    frame.view = view.view;
    frame.cameraPosition = view.cameraPosition;
    frame.sunDirection = sunDirection;
    frame.sunIntensity = view.sun.intensity;
    frame.sunColor = view.sun.color;
    frame.ambient = view.ambient;
    frame.cascadeCount = m_settings.cascadeCount;
    if (m_settings.cascadeCount > 0)
    {
        const auto splits =
            CascadeSplits(view.nearPlane, m_settings.shadowDistance, m_settings.cascadeCount, m_settings.cascadeLambda);
        for (std::uint32_t i = 0; i < m_settings.cascadeCount; ++i)
        {
            const CascadeFit fit = FitCascade(view.view, view.verticalFov, aspect, splits[i], splits[i + 1],
                                              sunDirection, m_settings.shadowMapSize);
            frame.cascades[i] = fit.viewProjection;
            frame.cascadeSplits[i] = splits[i + 1];
            frame.cascadeTexels[i] = fit.texelWorldSize;
        }
    }
    frame.lightCount = static_cast<std::uint32_t>(view.pointLights.size());
    frame.lightsOffset = static_cast<std::uint32_t>(kLightsOffset);
    frame.screenWidth = static_cast<float>(width);
    frame.screenHeight = static_cast<float>(height);
    frame.clusterNear = view.nearPlane;
    frame.clusterFar = m_settings.clusterFar;
    frame.tanHalfFovY = std::tan(view.verticalFov * 0.5f);
    frame.aspect = aspect;

    std::byte* mapped = m_device.MappedMemory(sceneBuffer).data();
    std::memcpy(mapped, &frame, sizeof(frame));
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        const RenderObject& object = objects[i];
        const ObjectData data{.model = object.transform,
                              .normalMatrix = Transpose(Inverse(object.transform)),
                              .baseColor = object.material.baseColor,
                              .metallic = object.material.metallic,
                              .roughness = object.material.roughness,
                              .pad = {},
                              .boundsCenter = object.mesh.boundsCenter,
                              .boundsRadius = object.mesh.boundsRadius,
                              .firstIndex = object.mesh.firstIndex,
                              .indexCount = object.mesh.indexCount,
                              .vertexOffset = object.mesh.vertexOffset,
                              .flags = object.castsShadow ? kCastsShadow : 0u};
        std::memcpy(mapped + sizeof(FrameData) + i * sizeof(ObjectData), &data, sizeof(data));
    }
    if (!view.pointLights.empty())
    {
        std::memcpy(mapped + kLightsOffset, view.pointLights.data(), view.pointLights.size_bytes());
    }
    const std::uint32_t scene = m_device.BindlessIndex(sceneBuffer);
    const auto objectCount = static_cast<std::uint32_t>(objects.size());
    const std::uint32_t cascadeCount = m_settings.cascadeCount;
    const std::uint32_t viewCount = 1 + cascadeCount;
    const std::uint32_t vertices = m_device.BindlessIndex(m_meshes.Vertices());
    const rhi::BufferHandle indices = m_meshes.Indices();
    constexpr std::uint64_t kViewArguments = std::uint64_t{kDrawCommandSize} * kMaxObjects;

    // Object culling: one indirect command list per view (the camera, then each cascade).
    ForwardOutputs outputs;
    outputs.drawArguments = graph.CreateBuffer(
        "Draw arguments", {.size = kViewArguments * kMaxViews,
                           .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::CopySrc,
                           .debugName = "Draw arguments"});
    outputs.drawCounts =
        graph.CreateBuffer("Draw counts", {.size = std::uint64_t{4} * kMaxViews,
                                           .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect |
                                                    rhi::BufferUsage::CopyDst | rhi::BufferUsage::CopySrc,
                                           .debugName = "Draw counts"});
    graph.AddPass(
        "ClearDrawCounts", [&](PassBuilder& builder)
        { outputs.drawCounts = builder.Write(outputs.drawCounts, rhi::ResourceState::CopyDst); },
        [counts = outputs.drawCounts](PassContext& context)
        { context.Commands().FillBuffer(context.Buffer(counts), 0, std::uint64_t{4} * kMaxViews, 0); });
    graph.AddPass(
        "Cull",
        [&](PassBuilder& builder)
        {
            outputs.drawArguments = builder.Write(outputs.drawArguments, rhi::ResourceState::ShaderWrite);
            outputs.drawCounts = builder.Write(outputs.drawCounts, rhi::ResourceState::ShaderWrite);
        },
        [this, scene, objectCount, viewCount, arguments = outputs.drawArguments,
         counts = outputs.drawCounts](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            commands.BindPipeline(m_cull);
            for (std::uint32_t v = 0; v < viewCount; ++v)
            {
                commands.PushConstants(CullPush{.scene = scene,
                                                .objectCount = objectCount,
                                                .viewMatrixOffset = v == 0 ? 0u : kFrameCascadesOffset + (v - 1) * 64,
                                                .shadowView = v == 0 ? 0u : 1u,
                                                .arguments = m_device.BindlessIndex(context.Buffer(arguments)),
                                                .argumentsOffset = static_cast<std::uint32_t>(kViewArguments * v),
                                                .count = m_device.BindlessIndex(context.Buffer(counts)),
                                                .countOffset = 4 * v});
                commands.Dispatch((objectCount + 63) / 64, 1, 1);
            }
        });

    // Light culling.
    outputs.clusters = graph.CreateBuffer(
        "Light clusters", {.size = std::uint64_t{ClusterGrid::kClusterCount} * ClusterGrid::kClusterStride,
                           .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc,
                           .debugName = "Light clusters"});
    graph.AddPass(
        "LightCulling", [&](PassBuilder& builder)
        { outputs.clusters = builder.Write(outputs.clusters, rhi::ResourceState::ShaderWrite); },
        [this, scene, clusters = outputs.clusters](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            commands.BindPipeline(m_lightCulling);
            commands.PushConstants(
                CullingPush{.scene = scene, .clusters = m_device.BindlessIndex(context.Buffer(clusters))});
            commands.Dispatch((ClusterGrid::kClusterCount + 63) / 64, 1, 1);
        });

    // Sun shadow cascades.
    if (cascadeCount > 0)
    {
        const std::uint32_t size = m_settings.shadowMapSize;
        outputs.shadowMap = graph.CreateTexture("Shadow cascades",
                                                {.width = size,
                                                 .height = size,
                                                 .arrayLayers = cascadeCount,
                                                 .format = rhi::Format::D32Float,
                                                 .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled,
                                                 .debugName = "Shadow cascades"});
        graph.AddPass(
            "Shadows",
            [&](PassBuilder& builder)
            {
                builder.Read(outputs.drawArguments, rhi::ResourceState::IndirectArgument);
                builder.Read(outputs.drawCounts, rhi::ResourceState::IndirectArgument);
                outputs.shadowMap = builder.Write(outputs.shadowMap, rhi::ResourceState::DepthWrite);
            },
            [this, scene, size, cascadeCount, vertices, indices, outputs](PassContext& context)
            {
                rhi::ICommandList& commands = context.Commands();
                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    const rhi::DepthAttachment depth{.texture = context.Texture(outputs.shadowMap),
                                                     .arrayLayer = cascade};
                    commands.BeginRendering({.depth = &depth, .width = size, .height = size});
                    commands.BindPipeline(m_shadow);
                    commands.PushConstants(
                        ShadowPush{.vertices = vertices, .scene = scene, .cascade = cascade, .pad = 0});
                    commands.BindIndexBuffer(indices, 0, rhi::IndexFormat::Uint32);
                    const std::uint32_t view = 1 + cascade;
                    commands.DrawIndexedIndirectCount(context.Buffer(outputs.drawArguments), kViewArguments * view,
                                                      context.Buffer(outputs.drawCounts), 4ull * view, kMaxObjects,
                                                      kDrawCommandSize);
                    commands.EndRendering();
                }
            });
    }

    // Shading.
    FGTexture depth = graph.CreateTexture("Forward depth", {.width = width,
                                                            .height = height,
                                                            .format = rhi::Format::D32Float,
                                                            .usage = rhi::TextureUsage::DepthTarget,
                                                            .debugName = "Forward depth"});
    graph.AddPass(
        "Forward",
        [&](PassBuilder& builder)
        {
            builder.Read(outputs.drawArguments, rhi::ResourceState::IndirectArgument);
            builder.Read(outputs.drawCounts, rhi::ResourceState::IndirectArgument);
            builder.Read(outputs.clusters, rhi::ResourceState::ShaderRead);
            if (outputs.shadowMap.IsValid())
            {
                builder.Read(outputs.shadowMap, rhi::ResourceState::ShaderRead);
            }
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
            depth = builder.Write(depth, rhi::ResourceState::DepthWrite);
        },
        [this, color, depth, width, height, scene, vertices, indices, clearColor, outputs](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{
                .texture = context.Texture(color), .clearColor = {clearColor.x, clearColor.y, clearColor.z, 1.0f}}};
            const rhi::DepthAttachment depthAttachment{.texture = context.Texture(depth)};
            commands.BeginRendering({.colors = colors, .depth = &depthAttachment, .width = width, .height = height});
            commands.BindPipeline(m_forward);
            const std::uint32_t shadowMap = outputs.shadowMap.IsValid()
                                                ? m_device.BindlessIndex(context.Texture(outputs.shadowMap))
                                                : rhi::kInvalidBindlessIndex;
            commands.PushConstants(ForwardPush{.vertices = vertices,
                                               .scene = scene,
                                               .pad0 = 0,
                                               .clusters = m_device.BindlessIndex(context.Buffer(outputs.clusters)),
                                               .shadowMap = shadowMap,
                                               .shadowSampler = m_device.BindlessIndex(m_shadowSampler),
                                               .pad = {}});
            commands.BindIndexBuffer(indices, 0, rhi::IndexFormat::Uint32);
            commands.DrawIndexedIndirectCount(context.Buffer(outputs.drawArguments), 0,
                                              context.Buffer(outputs.drawCounts), 0, kMaxObjects, kDrawCommandSize);
            commands.EndRendering();
        });
    return outputs;
}

} // namespace interi::render
