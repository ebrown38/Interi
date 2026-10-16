// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/Window.h>
#include <interi/render/Decals.h>
#include <interi/render/Particles.h>

#include <memory>
#include <span>

namespace interi
{
class JobSystem;
class SdlPlatform;
class World;
} // namespace interi

namespace interi::destruction
{
class DestructionWorld;
struct DestructibleAsset;
} // namespace interi::destruction

namespace sandbox
{

/// What one rendered frame shows.
struct SandboxFrame
{
    std::span<const interi::Transform> cubes; ///< Debris cube poses (scale ignored).
    /// Destructible pieces, debris and rubble, and the crack decals of damaged bonds (both or neither).
    interi::World* world = nullptr;
    const interi::destruction::DestructionWorld* destruction = nullptr;
    std::span<const interi::render::Particle> particles; ///< Emitted since the previous frame.
    std::span<const interi::render::Decal> decals;       ///< Extra decals (scorch marks).
    double seconds = 0.0;                                ///< Animates the camera.
    float dt = 0.0f;                                     ///< Seconds since the previous frame (particles).
};

/// Draws the sandbox scene (ground, pillars, debris cubes, destructibles with interior materials, GPU particles,
/// crack and scorch decals, sun and point lights) to the window with the Vulkan forward renderer: swapchain, frames
/// in flight, frame graph, GPU culling and shadows.
class SandboxRenderer
{
public:
    /// Fails (with the reason) when the platform or machine has no usable Vulkan, e.g. the "dummy" video driver.
    [[nodiscard]] static interi::Result<std::unique_ptr<SandboxRenderer>>
    Create(interi::SdlPlatform& platform, interi::IWindow& window, interi::JobSystem& jobs, bool validation);
    ~SandboxRenderer();

    SandboxRenderer(const SandboxRenderer&) = delete;
    SandboxRenderer& operator=(const SandboxRenderer&) = delete;
    SandboxRenderer(SandboxRenderer&&) = delete;
    SandboxRenderer& operator=(SandboxRenderer&&) = delete;

    /// Uploads the chunk meshes of a destructible asset (once per asset) so its instances are drawn.
    [[nodiscard]] interi::Result<void>
    AddAsset(const std::shared_ptr<const interi::destruction::DestructibleAsset>& asset);

    /// Renders and presents one frame. Returns whether a frame was presented (false while minimised or on errors,
    /// which are logged).
    bool Render(const SandboxFrame& frame);

    /// Where the camera is at `seconds` (it circles the scene).
    [[nodiscard]] static interi::Vec3 CameraPosition(double seconds);

private:
    struct State;
    explicit SandboxRenderer(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace sandbox
