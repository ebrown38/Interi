// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/render/Clusters.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/MeshPool.h>
#include <interi/rhi/Types.h>

#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

namespace interi::rhi
{
class IDevice;
class ResourceManager;
} // namespace interi::rhi

namespace interi::render
{

/// Metallic-roughness material parameters (constant per object for now).
struct Material
{
    Vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct RenderObject
{
    MeshRange mesh{}; ///< In the renderer's MeshPool. NOLINT(readability-redundant-member-init)
    Mat4 transform = Mat4::Identity();
    Material material{}; // NOLINT(readability-redundant-member-init): lets designated inits omit it
    bool castsShadow = true;
};

struct DirectionalLight
{
    Vec3 direction{0.0f, -1.0f, 0.0f}; ///< The direction the light travels (from the light into the scene).
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 3.0f;
};

/// The camera and lighting of a frame. The projection is perspective with reverse-Z and an infinite far plane
/// (PerspectiveReverseZ), using the target's aspect ratio.
struct SceneView
{
    Mat4 view = Mat4::Identity();
    Vec3 cameraPosition;
    float verticalFov = std::numbers::pi_v<float> / 3.0f;
    float nearPlane = 0.1f;
    Vec3 ambient{0.03f, 0.03f, 0.03f};
    DirectionalLight sun;
    std::span<const PointLight> pointLights{}; // NOLINT(readability-redundant-member-init)
};

struct ForwardSettings
{
    std::uint32_t framesInFlight = 2;
    std::uint32_t cascadeCount = 4;     ///< Sun shadow cascades (0 disables sun shadows).
    std::uint32_t shadowMapSize = 1024; ///< Texels per side of each cascade.
    float shadowDistance = 50.0f;       ///< View distance beyond which nothing receives sun shadows.
    float cascadeLambda = 0.75f;        ///< Logarithmic vs uniform cascade splits (see CascadeSplits).
    float clusterFar = 100.0f;          ///< Point lights are shaded up to this view distance.
};

/// Graph resources the forward passes produce, for later passes (and tests) to read.
struct ForwardOutputs
{
    FGBuffer clusters;      ///< Per-cluster light lists (ClusterGrid layout).
    FGTexture shadowMap;    ///< Cascade depth array; invalid when shadows are disabled.
    FGBuffer drawArguments; ///< Culled DrawIndexedIndirect commands: view v starts at v * kMaxObjects * 20 bytes.
    FGBuffer drawCounts;    ///< uint32 draw count per view (view 0 = camera, 1 + i = shadow cascade i).
};

/// GPU-driven, physically based forward rendering through the frame graph. Per frame: a compute pass culls every
/// object against the camera and each shadow cascade and writes indirect draw commands; clustered point-light
/// culling (compute); sun shadow cascades; then one forward pass shading the visible objects. All draws are
/// DrawIndexedIndirectCount over the MeshPool's shared geometry.
///
/// Scene data (camera, lights, objects) goes into a persistently mapped buffer with one slot per frame in flight;
/// AddPasses writes the next slot, so it must not be called for frame N + framesInFlight before frame N finished on
/// the GPU.
class ForwardRenderer
{
public:
    static constexpr std::uint32_t kMaxObjects = 4096;
    static constexpr std::uint32_t kMaxPointLights = 1024;

    /// Objects reference meshes in `meshes`. `colorFormat` is the format of the targets passed to AddPasses.
    [[nodiscard]] static Result<std::unique_ptr<ForwardRenderer>>
    Create(rhi::IDevice& device, rhi::ResourceManager& resources, const MeshPool& meshes, rhi::Format colorFormat,
           const ForwardSettings& settings = {});
    ~ForwardRenderer();

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;
    ForwardRenderer(ForwardRenderer&&) = delete;
    ForwardRenderer& operator=(ForwardRenderer&&) = delete;

    /// Adds the passes that clear `color` (width x height) to `clearColor` and draw `objects`. `color` is replaced
    /// by the version the forward pass writes.
    ForwardOutputs AddPasses(FrameGraph& graph, FGTexture& color, std::uint32_t width, std::uint32_t height,
                             const SceneView& view, std::span<const RenderObject> objects, Vec3 clearColor);

private:
    ForwardRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, const MeshPool& meshes,
                    const ForwardSettings& settings);

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    const MeshPool& m_meshes;
    ForwardSettings m_settings;
    rhi::PipelineHandle m_cull;
    rhi::PipelineHandle m_forward;
    rhi::PipelineHandle m_shadow;
    rhi::PipelineHandle m_lightCulling;
    rhi::SamplerHandle m_shadowSampler;
    std::vector<rhi::BufferHandle> m_sceneBuffers; ///< One per frame in flight.
    std::uint32_t m_nextSlot = 0;
};

} // namespace interi::render
