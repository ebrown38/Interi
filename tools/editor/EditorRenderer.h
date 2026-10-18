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
#include <string>

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

namespace interi::scene
{
struct TriangleMesh;
} // namespace interi::scene

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

/// A scene mesh (primitive or mesh asset, added with AddMesh) drawn with one colour per material slot (the last
/// colour repeats; `color`, when set, overrides them all: selection).
struct EditorMesh
{
    const scene::TriangleMesh* mesh = nullptr;
    Mat4 transform;
    std::span<const Vec3> slotColors;
    std::optional<Vec3> color;
    bool occluder = true;
};

/// What one editor frame shows: the 3D scene in the viewport texture, then the UI over the whole window.
struct EditorFrame
{
    std::span<const EditorBox> boxes;
    std::span<const EditorChunk> chunks;
    std::span<const EditorMesh> meshes;
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

    /// Uploads every chunk mesh of an asset (once per asset). When the mesh pool is full it is cleared and started
    /// over, so call this every frame for each asset drawn (it returns at once for cached ones).
    [[nodiscard]] Result<void> AddAsset(const std::shared_ptr<const destruction::DestructibleAsset>& asset);
    /// Uploads a scene mesh, one range per material slot (once per mesh object; the renderer keeps it alive while
    /// cached). Same pool rules as AddAsset: call it every frame for each mesh drawn.
    [[nodiscard]] Result<void> AddMesh(const std::shared_ptr<const scene::TriangleMesh>& mesh);

    /// The ImTextureID of a `width` x `height` viewport texture for this frame (recreated when the size changes),
    /// or 0 for an empty size. Call during the UI frame, before Render.
    [[nodiscard]] std::uint64_t ViewportTexture(std::uint32_t width, std::uint32_t height);

    /// Renders the scene into the viewport texture and the UI to the window. False when nothing was presented
    /// (minimised, or an error, which is logged).
    bool Render(const EditorFrame& frame);

    /// The next Render draws the UI into an offscreen image instead of the window and writes it to `path` as a
    /// binary PPM (debugging: --screenshot). That frame is not presented.
    void CaptureNextFrame(std::string path) { m_capturePath = std::move(path); }

private:
    struct State;
    explicit EditorRenderer(std::unique_ptr<State> state);
    [[nodiscard]] bool Capture(const EditorFrame& frame, render::FGTexture viewport);
    /// Clears the mesh pool (waiting for the GPU) and re-adds the built-in cube.
    [[nodiscard]] Result<void> ResetMeshPool();

    std::unique_ptr<State> m_state;
    std::string m_capturePath;
};

} // namespace interi::editor
