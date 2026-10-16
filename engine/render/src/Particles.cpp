// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/render/Particles.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/shaders/Particle.ps.h>
#include <interi/shaders/Particle.vs.h>
#include <interi/shaders/Particles.cs.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace interi::render
{

namespace
{

// Must match engine/render/shaders/ParticleCommon.hlsli.
struct FrameHeader
{
    Mat4 viewProjection;
    Mat4 view;
    Vec3 gravity;
    float dt;
    std::uint32_t emitCount;
    std::uint32_t emitStart;
    std::uint32_t capacity;
    float nearPlane;
    std::uint32_t pad[8];
};
static_assert(sizeof(FrameHeader) == 192);

struct ComputePush
{
    std::uint32_t frame;
    std::uint32_t pool;
    std::uint32_t mode;
};

struct DrawPush
{
    std::uint32_t frame;
    std::uint32_t pool;
    std::uint32_t depth;
    std::uint32_t additive;
};

constexpr std::uint32_t kEmitMode = 0;
constexpr std::uint32_t kSimulateMode = 1;

} // namespace

void IntegrateParticles(std::span<Particle> particles, Vec3 gravity, float dt) noexcept
{
    for (Particle& p : particles)
    {
        if (p.life <= 0.0f)
        {
            continue;
        }
        p.velocity += gravity * p.gravityScale * dt;
        p.velocity *= std::max(1.0f - p.drag * dt, 0.0f);
        p.position += p.velocity * dt;
        p.life -= dt;
    }
}

GpuParticleSystem::GpuParticleSystem(rhi::IDevice& device, rhi::ResourceManager& resources,
                                     const ParticleSettings& settings)
    : m_device(device)
    , m_resources(resources)
    , m_settings(settings)
{
}

Result<std::unique_ptr<GpuParticleSystem>> GpuParticleSystem::Create(rhi::IDevice& device,
                                                                     rhi::ResourceManager& resources,
                                                                     rhi::Format colorFormat,
                                                                     const ParticleSettings& settings)
{
    INTERI_ASSERT(settings.capacity > 0 && settings.maxEmitPerFrame > 0 && settings.framesInFlight > 0);
    std::unique_ptr<GpuParticleSystem> system(new GpuParticleSystem(device, resources, settings));
    INTERI_TRY_ASSIGN(compute, device.CreateComputePipeline(
                                   {.code = std::as_bytes(std::span(kParticlesCs)), .debugName = "Particles"}));
    system->m_compute = compute;
    const std::array formats{colorFormat};
    for (const rhi::BlendMode blend : {rhi::BlendMode::Alpha, rhi::BlendMode::Additive})
    {
        INTERI_TRY_ASSIGN(pipeline, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kParticleVs)),
                                                                   .pixelCode = std::as_bytes(std::span(kParticlePs)),
                                                                   .colorFormats = formats,
                                                                   .cullMode = rhi::CullMode::None,
                                                                   .depthTest = false,
                                                                   .depthWrite = false,
                                                                   .blend = blend,
                                                                   .debugName = "Particles"}));
        (blend == rhi::BlendMode::Alpha ? system->m_alpha : system->m_additive) = pipeline;
    }
    INTERI_TRY_ASSIGN(pool, device.CreateBuffer({.size = std::uint64_t{sizeof(Particle)} * settings.capacity,
                                                 .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::CopySrc |
                                                          rhi::BufferUsage::CopyDst,
                                                 .debugName = "Particle ring"}));
    system->m_pool = pool;
    for (std::uint32_t i = 0; i < settings.framesInFlight; ++i)
    {
        INTERI_TRY_ASSIGN(frame, device.CreateBuffer({.size = sizeof(FrameHeader) + std::uint64_t{sizeof(Particle)} *
                                                                                        settings.maxEmitPerFrame,
                                                      .usage = rhi::BufferUsage::Storage,
                                                      .memory = rhi::MemoryUsage::Upload,
                                                      .debugName = "Particle frame"}));
        system->m_frames.push_back(frame);
    }
    return system;
}

GpuParticleSystem::~GpuParticleSystem()
{
    for (const rhi::PipelineHandle pipeline : {m_compute, m_alpha, m_additive})
    {
        if (pipeline.IsValid())
        {
            m_resources.DestroyPipeline(pipeline);
        }
    }
    if (m_pool.IsValid())
    {
        m_resources.DestroyBuffer(m_pool);
    }
    for (const rhi::BufferHandle frame : m_frames)
    {
        m_resources.DestroyBuffer(frame);
    }
}

std::uint32_t GpuParticleSystem::Emit(std::span<const Particle> particles)
{
    const std::size_t room = m_settings.maxEmitPerFrame - m_pending.size();
    const std::size_t accepted = std::min(room, particles.size());
    m_pending.insert(m_pending.end(), particles.begin(), particles.begin() + static_cast<std::ptrdiff_t>(accepted));
    return static_cast<std::uint32_t>(accepted);
}

void GpuParticleSystem::AddPasses(FrameGraph& graph, float dt, const ParticleView& view, FGTexture& color,
                                  FGTexture depth, std::uint32_t width, std::uint32_t height)
{
    const rhi::BufferHandle frameBuffer = m_frames[m_nextFrame];
    m_nextFrame = (m_nextFrame + 1) % static_cast<std::uint32_t>(m_frames.size());

    const auto emitCount = static_cast<std::uint32_t>(m_pending.size());
    const FrameHeader header{.viewProjection = view.viewProjection,
                             .view = view.view,
                             .gravity = m_settings.gravity,
                             .dt = dt,
                             .emitCount = emitCount,
                             .emitStart = m_head,
                             .capacity = m_settings.capacity,
                             .nearPlane = view.nearPlane,
                             .pad = {}};
    std::byte* mapped = m_device.MappedMemory(frameBuffer).data();
    std::memcpy(mapped, &header, sizeof(header));
    if (emitCount > 0)
    {
        std::memcpy(mapped + sizeof(FrameHeader), m_pending.data(), m_pending.size() * sizeof(Particle));
    }
    m_head = (m_head + emitCount) % m_settings.capacity;
    m_pending.clear();

    const std::uint32_t frame = m_device.BindlessIndex(frameBuffer);
    const std::uint32_t capacity = m_settings.capacity;
    FGBuffer pool = graph.ImportBuffer("Particle ring", m_pool,
                                       m_poolCleared ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined,
                                       rhi::ResourceState::ShaderRead);
    if (!m_poolCleared)
    {
        // Zero life: every slot starts free.
        graph.AddPass(
            "ParticleClear", [&](PassBuilder& builder) { pool = builder.Write(pool, rhi::ResourceState::CopyDst); },
            [pool, capacity](PassContext& context)
            { context.Commands().FillBuffer(context.Buffer(pool), 0, std::uint64_t{sizeof(Particle)} * capacity, 0); });
        m_poolCleared = true;
    }
    graph.AddPass(
        "ParticleSimulate", [&](PassBuilder& builder) { pool = builder.Write(pool, rhi::ResourceState::ShaderWrite); },
        [this, pool, frame, emitCount, capacity](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const rhi::BufferHandle ring = context.Buffer(pool);
            commands.BindPipeline(m_compute);
            const std::uint32_t poolIndex = m_device.BindlessIndex(ring);
            if (emitCount > 0)
            {
                commands.PushConstants(ComputePush{.frame = frame, .pool = poolIndex, .mode = kEmitMode});
                commands.Dispatch((emitCount + 63) / 64, 1, 1);
                // The integration reads what the emit wrote.
                const std::array between{
                    rhi::BufferBarrier{ring, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderWrite}};
                commands.Barriers(between, {});
            }
            commands.PushConstants(ComputePush{.frame = frame, .pool = poolIndex, .mode = kSimulateMode});
            commands.Dispatch((capacity + 63) / 64, 1, 1);
        });

    if (!color.IsValid())
    {
        return; // the ring is imported, so the simulation is an output of the graph without a draw
    }
    graph.AddPass(
        "Particles",
        [&](PassBuilder& builder)
        {
            builder.Read(pool, rhi::ResourceState::ShaderRead);
            builder.Read(depth, rhi::ResourceState::ShaderRead);
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
        },
        [this, pool, depth, color, frame, width, height, capacity](PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{.texture = context.Texture(color), .load = rhi::LoadOp::Load}};
            commands.BeginRendering({.colors = colors, .width = width, .height = height});
            const DrawPush push{.frame = frame,
                                .pool = m_device.BindlessIndex(context.Buffer(pool)),
                                .depth = m_device.BindlessIndex(context.Texture(depth)),
                                .additive = 0};
            commands.BindPipeline(m_alpha);
            commands.PushConstants(push);
            commands.Draw(capacity * 6, 1, 0, 0);
            commands.BindPipeline(m_additive);
            DrawPush additive = push;
            additive.additive = 1;
            commands.PushConstants(additive);
            commands.Draw(capacity * 6, 1, 0, 0);
            commands.EndRendering();
        });
}

} // namespace interi::render
