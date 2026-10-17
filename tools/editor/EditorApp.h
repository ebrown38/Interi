// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/FracturePreview.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/FracturePanel.h>
#include <interi/editor_ui/ImGuiInput.h>
#include <interi/platform/FileSystem.h>

#include "EditorRenderer.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;

namespace interi::editor
{

struct EditorAppDesc
{
    std::filesystem::path dataDir; ///< The data/ tree: levels, assets.
    bool validation = false;       ///< Vulkan validation layers.
};

struct FrameResult
{
    bool presented = false;
    bool quit = false;
};

/// The editor application: one document, the UI and the viewport. Frame() runs one frame.
class EditorApp
{
public:
    EditorApp(SdlPlatform& platform, IWindow& window, JobSystem& jobs, const EditorAppDesc& desc);
    ~EditorApp();

    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;
    EditorApp(EditorApp&&) = delete;
    EditorApp& operator=(EditorApp&&) = delete;

    /// Opens a level by its path under the data directory.
    [[nodiscard]] Result<void> OpenLevel(std::string_view path);
    [[nodiscard]] FrameResult Frame();
    /// Opens a glTF under the data directory in the Fracture panel and previews it.
    void OpenFractureSource(const std::string& path) { OpenSource(path); }
    /// Writes the next frame to a PPM file instead of presenting it.
    void CaptureNextFrame(std::string path)
    {
        if (m_renderer != nullptr)
        {
            m_renderer->CaptureNextFrame(std::move(path));
        }
    }

    [[nodiscard]] EditorDocument& Document() noexcept { return m_document; }

private:
    void DrawViewport();
    /// Picking, gizmo drags and keyboard shortcuts while the mouse is over the viewport image.
    void HandleViewportInput(const ScreenRect& viewport);
    void ApplyDrag(const Ray& ray);
    /// Places a dropped asset where the mouse ray meets the ground (y = 0) or the entity under it.
    void DropAsset(const std::string& path, const Ray& ray);
    [[nodiscard]] Vec3 PlacementPoint(const Ray& ray) const;
    [[nodiscard]] AssetBoundsFn Bounds();
    void BuildScene();

    // Fracture authoring.
    [[nodiscard]] bool PreviewActive() const;
    void OpenSource(const std::string& path);
    void SaveAndCook();
    void BuildPreviewScene();
    /// Adds a seed stroke where the ray meets the edited object (or any object for the defaults).
    void PaintStroke(const Ray& ray);
    void DrawPreviewOverlay(const ScreenRect& viewport);
    /// Cooks a glTF under the data directory to its `.idest` (if out of date) and returns that path.
    [[nodiscard]] Result<std::string> CookSource(const std::string& path);
    /// A destructible asset by virtual path, loaded (and uploaded) once; nullptr if it can't be read.
    [[nodiscard]] std::shared_ptr<const destruction::DestructibleAsset> Asset(const std::string& path);

    SdlPlatform& m_platform;
    IWindow& m_window;
    JobSystem& m_jobs;
    std::filesystem::path m_dataDir;
    NativeFileSystem m_dataFiles;
    MemoryFileSystem m_generatedFiles;
    VirtualFileSystem m_files;
    std::string m_iniPath;
    ImGuiContext* m_context = nullptr;
    ImGuiInput m_input;
    std::unique_ptr<EditorRenderer> m_renderer;
    EditorDocument m_document;
    OrbitCamera m_camera;
    ToolSettings m_tools;
    std::vector<AssetEntry> m_assetList;
    /// The gizmo drag in progress: what it moves and that entity's state when it started.
    std::optional<GizmoDrag> m_drag;
    EditorEntityId m_dragTarget = 0;
    Transform m_dragStart;
    Vec3 m_dragStartHalfExtents;
    std::uint64_t m_dragKey = 0;
    GizmoAxis m_hoveredAxis = GizmoAxis::None;
    FracturePreview m_preview;
    FractureTool m_fracture;
    bool m_painting = false;
    std::optional<Vec3> m_lastStroke;
    std::map<std::string, std::shared_ptr<const destruction::DestructibleAsset>, std::less<>> m_assets;
    std::set<std::string, std::less<>> m_missingAssets;
    std::vector<EditorBox> m_boxes;
    std::vector<EditorChunk> m_chunks;
    float m_viewportWidth = 0.0f;
    float m_viewportHeight = 0.0f;
    bool m_buildLayout = false;
    std::chrono::steady_clock::time_point m_previous;
};

} // namespace interi::editor
