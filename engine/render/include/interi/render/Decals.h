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

enum class DecalKind : std::uint8_t
{
    /// A jagged crack line where the surface meets the box's local z = 0 plane (e.g. a bond's face).
    Crack = 0,
    /// Soot fading out from the box centre (3D distance, so it wraps around corners).
    Scorch = 1,
};

/// A box decal: whatever opaque surface lies inside the box [-0.5, 0.5]^3 (`transform` maps it to the world) is
/// darkened towards `color` by the kind's procedural pattern times `intensity`.
struct Decal
{
    Mat4 transform = Mat4::Identity();
    Vec3 color{0.05f, 0.05f, 0.05f};
    float intensity = 1.0f; ///< 0..1.
    DecalKind kind = DecalKind::Crack;
    std::uint32_t seed = 0; ///< Varies the pattern.
};

/// The box transform for a decal centred on `center`, with local z along `normal` and the given extents (m)
/// across (x, y) and along (z) it.
[[nodiscard]] Mat4 DecalBox(Vec3 center, Vec3 normal, float across, float along) noexcept;

struct DecalView
{
    Mat4 viewProjection = Mat4::Identity();
};

/// Deferred (screen-space) decals: each box is drawn after the forward pass, and its pixel shader reconstructs the
/// surface position from the scene depth, keeps the pixels inside the box and alpha-blends the pattern over the
/// colour target. The decal list goes into one upload slot per frame in flight.
class DecalRenderer
{
public:
    [[nodiscard]] static Result<std::unique_ptr<DecalRenderer>>
    Create(rhi::IDevice& device, rhi::ResourceManager& resources, rhi::Format colorFormat,
           std::uint32_t maxDecals = 4096, std::uint32_t framesInFlight = 2);
    ~DecalRenderer();

    DecalRenderer(const DecalRenderer&) = delete;
    DecalRenderer& operator=(const DecalRenderer&) = delete;
    DecalRenderer(DecalRenderer&&) = delete;
    DecalRenderer& operator=(DecalRenderer&&) = delete;

    /// Draws `decals` (at most maxDecals; the rest are dropped) over `color` (width x height) using the forward
    /// pass's `depth`. Adds nothing when `decals` is empty.
    void AddPasses(FrameGraph& graph, std::span<const Decal> decals, const DecalView& view, FGTexture& color,
                   FGTexture depth, std::uint32_t width, std::uint32_t height);

private:
    DecalRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, std::uint32_t maxDecals);

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    std::uint32_t m_maxDecals;
    rhi::PipelineHandle m_pipeline;
    std::vector<rhi::BufferHandle> m_frames;
    std::uint32_t m_nextFrame = 0;
};

} // namespace interi::render
