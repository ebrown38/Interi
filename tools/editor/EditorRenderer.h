// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/Window.h>
#include <interi/render/ForwardRenderer.h>

#include <memory>
#include <optional>
#include <span>

struct ImDrawData;

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

namespace interi::editor
{
class ImGuiRenderer;

/// A box: `transform` maps the unit cube (-0.5 .. 0.5) into the world.
struct EditorBox
{
    Mat4 transform;
    Vec3 color;
    bool occluder = false;
};

/// One chunk of a destructible asset (added with AddAsset), with its own materials or one colour.
struct EditorChunk
{
    const destruction::DestructibleAsset* asset = nullptr;
    std::uint32_t chunk = 0;
    Mat4 transform;
    std::optional<Vec3> color; ///< Overrides the chunk's materials (cell preview, selection).
};

/// What one editor frame shows: the 3D scene in the viewport texture, then the UI over the whole window.
struct EditorFrame
{
    std::span<const EditorBox> boxes;
    std::span<const EditorChunk> chunks;
    /// Play mode: destructible pieces, debris and crack decals from the running simulation (both or neither).
    World* world = nullptr;
    const destruction::DestructionWorld* destruction = nullptr;
    render::SceneView view;
    ImDrawData* ui = nullptr;
};

/// The editor's GPU side: the forward renderer draws the scene into an offscreen viewport texture that the UI shows
/// with ImGui::Image, then the UI is drawn to the swapchain.
class EditorRenderer
{
public:
    /// Creates the device, swapchain and renderers. The ImGui context must exist (the UI renderer registers itself
    /// as its backend). Fails when the platform or machine has no usable Vulkan.
    [[nodiscard]] static Result<std::unique_ptr<EditorRenderer>> Create(SdlPlatform& platform, IWindow& window,
                                                                        JobSystem& jobs, bool validation);
    ~EditorRenderer();

    EditorRenderer(const EditorRenderer&) = delete;
    EditorRenderer& operator=(const EditorRenderer&) = delete;
    EditorRenderer(EditorRenderer&&) = delete;
    EditorRenderer& operator=(EditorRenderer&&) = delete;

    /// Uploads every chunk mesh of an asset (once per asset).
    [[nodiscard]] Result<void> AddAsset(const std::shared_ptr<const destruction::DestructibleAsset>& asset);

    /// The ImTextureID of a `width` x `height` viewport texture for this frame (recreated when the size changes),
    /// or 0 for an empty size. Call during the UI frame, before Render.
    [[nodiscard]] std::uint64_t ViewportTexture(std::uint32_t width, std::uint32_t height);

    /// Renders the scene into the viewport texture and the UI to the window. False when nothing was presented
    /// (minimised, or an error, which is logged).
    bool Render(const EditorFrame& frame);

private:
    struct State;
    explicit EditorRenderer(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace interi::editor
