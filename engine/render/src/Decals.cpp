// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Mat4.h>
#include <interi/render/Decals.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/shaders/Decal.ps.h>
#include <interi/shaders/Decal.vs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace interi::render
{

namespace
{

// Must match engine/render/shaders/DecalCommon.hlsli.
struct DecalHeader
{
    Mat4 viewProjection;
    Mat4 inverseViewProjection;
    float screenWidth;
    float screenHeight;
    std::uint32_t pad[14];
};
static_assert(sizeof(DecalHeader) == 192);

struct DecalData
{
    Mat4 decalToWorld;
    Mat4 worldToDecal;
    Vec3 color;
    float intensity;
    std::uint32_t kind;
    std::uint32_t seed;
    std::uint32_t pad[2];
};
static_assert(sizeof(DecalData) == 160);

struct DecalPush
{
    std::uint32_t frame;
    std::uint32_t depth;
};

} // namespace

Mat4 DecalBox(Vec3 center, Vec3 normal, float across, float along) noexcept
{
    const Vec3 z = NormalizeOrZero(normal);
    const Vec3 helper = std::abs(z.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 x = Normalize(Cross(helper, z));
    const Vec3 y = Cross(z, x);
    Mat4 box;
    box.columns[0] = Vec4(x * across, 0.0f);
    box.columns[1] = Vec4(y * across, 0.0f);
    box.columns[2] = Vec4(z * along, 0.0f);
    box.columns[3] = Vec4(center, 1.0f);
    return box;
}

DecalRenderer::DecalRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, std::uint32_t maxDecals)
    : m_device(device)
    , m_resources(resources)
    , m_maxDecals(maxDecals)
{
}

Result<std::unique_ptr<DecalRenderer>> DecalRenderer::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                             rhi::Format colorFormat, std::uint32_t maxDecals,
                                                             std::uint32_t framesInFlight)
{
    INTERI_ASSERT(maxDecals > 0 && framesInFlight > 0);
    std::unique_ptr<DecalRenderer> renderer(new DecalRenderer(device, resources, maxDecals));
    const std::array formats{colorFormat};
    // Back faces without depth testing: the box covers its pixels even with the camera inside it, and the pixel
    // shader tests the reconstructed surface against the box.
    INTERI_TRY_ASSIGN(pipeline, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kDecalVs)),
                                                               .pixelCode = std::as_bytes(std::span(kDecalPs)),
                                                               .colorFormats = formats,
                                                               .cullMode = rhi::CullMode::Front,
                                                               .depthTest = false,
                                                               .depthWrite = false,
                                                               .blend = rhi::BlendMode::Alpha,
                                                               .debugName = "Decals"}));
    renderer->m_pipeline = pipeline;
    for (std::uint32_t i = 0; i < framesInFlight; ++i)
    {
        INTERI_TRY_ASSIGN(
            frame, device.CreateBuffer({.size = sizeof(DecalHeader) + std::uint64_t{sizeof(DecalData)} * maxDecals,
                                        .usage = rhi::BufferUsage::Storage,
                                        .memory = rhi::MemoryUsage::Upload,
                                        .debugName = "Decals"}));
        renderer->m_frames.push_back(frame);
    }
    return renderer;
}

DecalRenderer::~DecalRenderer()
{
    if (m_pipeline.IsValid())
    {
        m_resources.DestroyPipeline(m_pipeline);
    }
    for (const rhi::BufferHandle frame : m_frames)
    {
        m_resources.DestroyBuffer(frame);
    }
}

void DecalRenderer::AddPasses(FrameGraph& graph, std::span<const Decal> decals, const DecalView& view, FGTexture& color,
                              FGTexture depth, std::uint32_t width, std::uint32_t height)
{
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(decals.size(), m_maxDecals));
    if (count == 0)
    {
        return;
    }
    const rhi::BufferHandle frameBuffer = m_frames[m_nextFrame];
    m_nextFrame = (m_nextFrame + 1) % static_cast<std::uint32_t>(m_frames.size());
    std::byte* mapped = m_device.MappedMemory(frameBuffer).data();
    const DecalHeader header{.viewProjection = view.viewProjection,
                             .inverseViewProjection = Inverse(view.viewProjection),
                             .screenWidth = static_cast<float>(width),
                             .screenHeight = static_cast<float>(height),
                             .pad = {}};
    std::memcpy(mapped, &header, sizeof(header));
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const Decal& decal = decals[i];
        const DecalData data{.decalToWorld = decal.transform,
                             .worldToDecal = Inverse(decal.transform),
                             .color = decal.color,
                             .intensity = decal.intensity,
                             .kind = static_cast<std::uint32_t>(decal.kind),
                             .seed = decal.seed,
                             .pad = {}};
        const std::size_t offset = sizeof(DecalHeader) + i * sizeof(DecalData); // bytes: mapped is std::byte*
        std::memcpy(mapped + offset, &data, sizeof(data));
    }

    const std::uint32_t frame = m_device.BindlessIndex(frameBuffer);
    graph.AddPass(
        "Decals",
        [&](PassBuilder& builder)
        {
            builder.Read(depth, rhi::ResourceState::ShaderRead);
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
        },
        [this, color, depth, frame, count, width, height](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{.texture = context.Texture(color), .load = rhi::LoadOp::Load}};
            commands.BeginRendering({.colors = colors, .width = width, .height = height});
            commands.BindPipeline(m_pipeline);
            commands.PushConstants(DecalPush{.frame = frame, .depth = m_device.BindlessIndex(context.Texture(depth))});
            commands.Draw(count * 36, 1, 0, 0);
            commands.EndRendering();
        });
}

} // namespace interi::render
