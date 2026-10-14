// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/Window.h>

#include <memory>
#include <span>

namespace interi
{
class JobSystem;
class SdlPlatform;
} // namespace interi

namespace sandbox
{

/// Draws the sandbox scene (ground, pillars, debris cubes, sun and point lights) to the window with the Vulkan
/// forward renderer: swapchain, frames in flight, frame graph, GPU culling and shadows.
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

    /// Renders and presents one frame. `debris` are cube positions; `seconds` animates the camera. Returns whether a
    /// frame was presented (false while minimised or on errors, which are logged).
    bool Render(std::span<const interi::Vec3> debris, double seconds);

private:
    struct State;
    explicit SandboxRenderer(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace sandbox
