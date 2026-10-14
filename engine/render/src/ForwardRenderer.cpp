// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/Shadows.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
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
};
static_assert(sizeof(ObjectData) == 160);

struct ForwardPush
{
    std::uint32_t vertices;
    std::uint32_t scene;
    std::uint32_t objectIndex;
    std::uint32_t clusters;
    std::uint32_t shadowMap;
    std::uint32_t shadowSampler;
    std::uint32_t pad[2];
};

struct ShadowPush
{
    std::uint32_t vertices;
    std::uint32_t scene;
    std::uint32_t objectIndex;
    std::uint32_t cascade;
};

struct CullingPush
{
    std::uint32_t scene;
    std::uint32_t clusters;
};

constexpr std::uint64_t kLightsOffset = sizeof(FrameData) + sizeof(ObjectData) * ForwardRenderer::kMaxObjects;
constexpr std::uint64_t kSceneBufferSize = kLightsOffset + sizeof(PointLight) * ForwardRenderer::kMaxPointLights;

} // namespace

ForwardRenderer::ForwardRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, const ForwardSettings& settings)
    : m_device(device)
    , m_resources(resources)
    , m_settings(settings)
{
}

Result<std::unique_ptr<ForwardRenderer>> ForwardRenderer::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                                 rhi::Format colorFormat,
                                                                 const ForwardSettings& settings)
{
    INTERI_ASSERT(settings.framesInFlight > 0 && settings.cascadeCount <= kMaxShadowCascades);
    std::unique_ptr<ForwardRenderer> renderer(new ForwardRenderer(device, resources, settings));
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
    for (const rhi::PipelineHandle pipeline : {m_forward, m_shadow, m_lightCulling})
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
                              .pad = {}};
        std::memcpy(mapped + sizeof(FrameData) + i * sizeof(ObjectData), &data, sizeof(data));
    }
    if (!view.pointLights.empty())
    {
        std::memcpy(mapped + kLightsOffset, view.pointLights.data(), view.pointLights.size_bytes());
    }
    const std::vector<RenderObject> drawList(objects.begin(), objects.end());
    const std::uint32_t scene = m_device.BindlessIndex(sceneBuffer);

    // Light culling.
    ForwardOutputs outputs;
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
    const std::uint32_t cascadeCount = m_settings.cascadeCount;
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
            "Shadows", [&](PassBuilder& builder)
            { outputs.shadowMap = builder.Write(outputs.shadowMap, rhi::ResourceState::DepthWrite); },
            [this, scene, size, cascadeCount, drawList, shadowMap = outputs.shadowMap](PassContext& context)
            {
                rhi::ICommandList& commands = context.Commands();
                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    const rhi::DepthAttachment depth{.texture = context.Texture(shadowMap), .arrayLayer = cascade};
                    commands.BeginRendering({.depth = &depth, .width = size, .height = size});
                    commands.BindPipeline(m_shadow);
                    for (std::uint32_t i = 0; i < drawList.size(); ++i)
                    {
                        if (!drawList[i].castsShadow)
                        {
                            continue;
                        }
                        const GpuMesh& mesh = *drawList[i].mesh;
                        commands.PushConstants(ShadowPush{.vertices = m_device.BindlessIndex(mesh.vertices),
                                                          .scene = scene,
                                                          .objectIndex = i,
                                                          .cascade = cascade});
                        commands.BindIndexBuffer(mesh.indices, 0, rhi::IndexFormat::Uint32);
                        commands.DrawIndexed(mesh.indexCount, 1, 0, 0, 0);
                    }
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
            builder.Read(outputs.clusters, rhi::ResourceState::ShaderRead);
            if (outputs.shadowMap.IsValid())
            {
                builder.Read(outputs.shadowMap, rhi::ResourceState::ShaderRead);
            }
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
            depth = builder.Write(depth, rhi::ResourceState::DepthWrite);
        },
        [this, color, depth, width, height, scene, drawList, clearColor, outputs](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{
                .texture = context.Texture(color), .clearColor = {clearColor.x, clearColor.y, clearColor.z, 1.0f}}};
            const rhi::DepthAttachment depthAttachment{.texture = context.Texture(depth)};
            commands.BeginRendering({.colors = colors, .depth = &depthAttachment, .width = width, .height = height});
            commands.BindPipeline(m_forward);
            const std::uint32_t clusters = m_device.BindlessIndex(context.Buffer(outputs.clusters));
            const std::uint32_t shadowMap = outputs.shadowMap.IsValid()
                                                ? m_device.BindlessIndex(context.Texture(outputs.shadowMap))
                                                : rhi::kInvalidBindlessIndex;
            for (std::uint32_t i = 0; i < drawList.size(); ++i)
            {
                const GpuMesh& mesh = *drawList[i].mesh;
                commands.PushConstants(ForwardPush{.vertices = m_device.BindlessIndex(mesh.vertices),
                                                   .scene = scene,
                                                   .objectIndex = i,
                                                   .clusters = clusters,
                                                   .shadowMap = shadowMap,
                                                   .shadowSampler = m_device.BindlessIndex(m_shadowSampler),
                                                   .pad = {}});
                commands.BindIndexBuffer(mesh.indices, 0, rhi::IndexFormat::Uint32);
                commands.DrawIndexed(mesh.indexCount, 1, 0, 0, 0);
            }
            commands.EndRendering();
        });
    return outputs;
}

} // namespace interi::render
