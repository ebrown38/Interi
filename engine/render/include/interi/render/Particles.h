// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/render/FrameGraph.h>
#include <interi/rhi/Types.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace interi::rhi
{
class IDevice;
class ResourceManager;
} // namespace interi::rhi

namespace interi::render
{

/// One GPU particle (64 bytes; the layout of engine/render/shaders/ParticleCommon.hlsli). Camera-facing quads,
/// faded out over their lifetime and near opaque surfaces (soft particles).
struct Particle
{
    Vec3 position;
    float life = 0.0f; ///< Seconds left; <= 0 is a free slot.
    Vec3 velocity;
    float lifetime = 1.0f;   ///< Total seconds, for the fade.
    std::uint32_t color = 0; ///< sRGB RGBA8 (R in the low byte); alpha is the opacity at birth.
    float size = 0.1f;       ///< Quad edge length (m).
    float drag = 0.0f;       ///< Fraction of velocity lost per second.
    float gravityScale = 1.0f;
    std::uint32_t flags = 0;
    std::uint32_t pad[3] = {};
};
static_assert(sizeof(Particle) == 64, "shaders read particles with a 64-byte stride");

inline constexpr std::uint32_t kParticleAdditive = 1; ///< Blended additively (sparks); otherwise alpha (dust, smoke).

[[nodiscard]] constexpr std::uint32_t PackColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    return std::uint32_t{r} | (std::uint32_t{g} << 8) | (std::uint32_t{b} << 16) | (std::uint32_t{a} << 24);
}

/// CPU reference of the simulation step the GPU runs (same operations, same order): for each live particle,
/// v += g * gravityScale * dt; v *= max(1 - drag * dt, 0); p += v * dt; life -= dt.
void IntegrateParticles(std::span<Particle> particles, Vec3 gravity, float dt) noexcept;

struct ParticleSettings
{
    std::uint32_t capacity = 65536;        ///< Live particles; the oldest slots are overwritten when it wraps.
    std::uint32_t maxEmitPerFrame = 16384; ///< Emit calls beyond this per frame are dropped.
    std::uint32_t framesInFlight = 2;
    Vec3 gravity{0.0f, -9.81f, 0.0f};
};

/// Where and how particles are drawn this frame.
struct ParticleView
{
    Mat4 viewProjection = Mat4::Identity();
    Mat4 view = Mat4::Identity(); ///< World -> view (the quads face the camera).
    float nearPlane = 0.1f;       ///< Of the reverse-Z projection (to linearise scene depth for soft fades).
};

/// Particles simulated and drawn on the GPU. A ring of `capacity` particles lives in a GPU buffer: Emit queues new
/// particles, and each frame a compute pass copies them into the ring, a second one integrates every live particle
/// (IntegrateParticles), and two draws (alpha, then additive) render camera-facing quads over the colour target,
/// tested against the scene depth in the pixel shader.
///
/// The per-frame data (view, emitted particles) uses one upload slot per frame in flight, like ForwardRenderer.
class GpuParticleSystem
{
public:
    [[nodiscard]] static Result<std::unique_ptr<GpuParticleSystem>> Create(rhi::IDevice& device,
                                                                           rhi::ResourceManager& resources,
                                                                           rhi::Format colorFormat,
                                                                           const ParticleSettings& settings = {});
    ~GpuParticleSystem();

    GpuParticleSystem(const GpuParticleSystem&) = delete;
    GpuParticleSystem& operator=(const GpuParticleSystem&) = delete;
    GpuParticleSystem(GpuParticleSystem&&) = delete;
    GpuParticleSystem& operator=(GpuParticleSystem&&) = delete;

    /// Queues particles for the next AddPasses. Returns how many were accepted (the rest exceed maxEmitPerFrame).
    std::uint32_t Emit(std::span<const Particle> particles);

    /// Adds the emit and simulate passes, then, when `color` is valid, the draw pass over `color` (width x height)
    /// tested against `depth` (the forward pass's depth). The pool buffer is left readable by later passes.
    void AddPasses(FrameGraph& graph, float dt, const ParticleView& view, FGTexture& color, FGTexture depth,
                   std::uint32_t width, std::uint32_t height);

    /// The particle ring (Storage | CopySrc), e.g. to read it back in tests.
    [[nodiscard]] rhi::BufferHandle Pool() const noexcept { return m_pool; }
    [[nodiscard]] std::uint32_t Capacity() const noexcept { return m_settings.capacity; }

private:
    GpuParticleSystem(rhi::IDevice& device, rhi::ResourceManager& resources, const ParticleSettings& settings);

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    ParticleSettings m_settings;
    rhi::PipelineHandle m_compute;
    rhi::PipelineHandle m_alpha;
    rhi::PipelineHandle m_additive;
    rhi::BufferHandle m_pool;
    std::vector<rhi::BufferHandle> m_frames; ///< Upload slots: header, then the particles emitted that frame.
    std::uint32_t m_nextFrame = 0;
    std::vector<Particle> m_pending;
    std::uint32_t m_head = 0; ///< Ring slot of the next emitted particle.
    bool m_poolCleared = false;
};

} // namespace interi::render
