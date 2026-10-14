// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Mesh.h>
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

/// Metallic-roughness material parameters (constant per object for now).
struct Material
{
    Vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

struct RenderObject
{
    const GpuMesh* mesh = nullptr;
    Mat4 transform = Mat4::Identity();
    Material material;
};

struct DirectionalLight
{
    Vec3 direction{0.0f, -1.0f, 0.0f}; ///< The direction the light travels (from the light into the scene).
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 3.0f;
};

struct SceneView
{
    Mat4 view = Mat4::Identity();
    Mat4 projection = Mat4::Identity(); ///< Reverse-Z (see PerspectiveReverseZ).
    Vec3 cameraPosition;
    Vec3 ambient{0.03f, 0.03f, 0.03f};
    DirectionalLight sun;
};

/// Draws objects with physically based shading into a colour target, through the frame graph.
///
/// Scene data (camera, lights, per-object transforms and materials) goes into a persistently mapped buffer with one
/// slot per frame in flight; AddPasses writes the next slot, so it must not be called for frame N + framesInFlight
/// before frame N finished on the GPU.
class ForwardRenderer
{
public:
    static constexpr std::uint32_t kMaxObjects = 4096;

    /// `colorFormat` is the format of the targets passed to AddPasses.
    [[nodiscard]] static Result<std::unique_ptr<ForwardRenderer>> Create(rhi::IDevice& device,
                                                                         rhi::ResourceManager& resources,
                                                                         rhi::Format colorFormat,
                                                                         std::uint32_t framesInFlight);
    ~ForwardRenderer();

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;
    ForwardRenderer(ForwardRenderer&&) = delete;
    ForwardRenderer& operator=(ForwardRenderer&&) = delete;

    /// Adds the forward pass: clears `color` (width x height) to `clearColor` and draws `objects` with a transient
    /// depth buffer. `color` is replaced by the version the pass writes.
    void AddPasses(FrameGraph& graph, FGTexture& color, std::uint32_t width, std::uint32_t height,
                   const SceneView& view, std::span<const RenderObject> objects, Vec3 clearColor);

private:
    ForwardRenderer(rhi::IDevice& device, rhi::ResourceManager& resources);

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    rhi::PipelineHandle m_pipeline;
    std::vector<rhi::BufferHandle> m_sceneBuffers; ///< One per frame in flight.
    std::uint32_t m_nextSlot = 0;
};

} // namespace interi::render
