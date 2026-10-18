// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/DataEditing.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/FracturePreview.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>
#include <interi/editor/PlaySession.h>
#include <interi/editor_ui/DataPanels.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/FracturePanel.h>
#include <interi/editor_ui/ImGuiInput.h>
#include <interi/editor_ui/StructurePanel.h>
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
    /// A folder of module folders (each with a module.json): their data sections are edited in the Values,
    /// Characters and Data Tables panels. Empty: no project data.
    std::filesystem::path modulesDir;
    /// The project's module order (top = highest priority); empty: every module found, by id.
    std::vector<std::string> project;
    /// The project data file (ProjectData.h), loaded at startup if it exists and written with the level.
    std::filesystem::path projectDataPath;
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
    /// A scripted session for smoke runs (--demo): at `frame` of `frames`, plays the level, sets off an explosion at
    /// the middle wall, saves the session as a replay and verifies it.
    void DemoStep(std::uint64_t frame, std::uint64_t frames);
    /// The last replay message (e.g. the verify result).
    [[nodiscard]] const std::string& ReplayMessage() const noexcept { return m_replayMessage; }

    /// Writes the next frame to a PPM file instead of presenting it.
    void CaptureNextFrame(std::string path)
    {
        if (m_renderer != nullptr)
        {
            m_renderer->CaptureNextFrame(std::move(path));
        }
    }

    [[nodiscard]] EditorDocument& Document() noexcept { return m_document; }
    /// The project's module data, or nullptr without --modules.
    [[nodiscard]] ProjectData* Project() noexcept { return m_project.get(); }

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
    /// Resolves the modules of `desc` and loads the project data file (errors are logged; no project then).
    void OpenProject(const EditorAppDesc& desc);
    void SaveProjectData();

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

    // Play-in-editor and structural debugging.
    void StartPlay();
    /// Advances the session by the frame's time in fixed steps (at most a few per frame), applying a watched
    /// replay's events at their steps.
    void StepPlay(float seconds);
    void HandleStructureRequests(const StructureRequests& requests);
    void DrawStructureOverlay(const ScreenRect& viewport);
    [[nodiscard]] PlayStatus Status() const;
    /// A destructible asset by virtual path, loaded (and uploaded) once; nullptr if it can't be read.
    [[nodiscard]] std::shared_ptr<const destruction::DestructibleAsset> Asset(const std::string& path);
    /// The mesh an object draws (its primitive's, cached by parameters, or its mesh asset's); nullptr if none.
    [[nodiscard]] std::shared_ptr<const scene::TriangleMesh> ObjectMesh(Entity entity);

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
    std::unique_ptr<ProjectData> m_project;
    std::filesystem::path m_projectDataPath;
    DataPanelState m_dataPanels;
    OrbitCamera m_camera;
    ToolSettings m_tools;
    std::vector<AssetEntry> m_assetList;
    /// The gizmo drag in progress: what it moves and that entity's state when it started.
    std::optional<GizmoDrag> m_drag;
    EditorEntityId m_dragTarget = 0;
    Transform m_dragStart;
    std::uint64_t m_dragKey = 0;
    GizmoAxis m_hoveredAxis = GizmoAxis::None;
    FracturePreview m_preview;
    FractureTool m_fracture;
    bool m_painting = false;
    std::optional<Vec3> m_lastStroke;
    std::unique_ptr<PlaySession> m_play;
    std::optional<Replay> m_watching; ///< The replay being played back, if any.
    std::size_t m_nextReplayEvent = 0;
    StructureTool m_structure;
    std::string m_replayMessage;
    float m_playTime = 0.0f; ///< Seconds not yet stepped.
    std::map<std::string, std::shared_ptr<const destruction::DestructibleAsset>, std::less<>> m_assets;
    std::set<std::string, std::less<>> m_missingAssets;
    std::vector<EditorBox> m_boxes;
    std::vector<EditorChunk> m_chunks;
    std::vector<EditorMesh> m_meshes;
    std::vector<std::vector<Vec3>> m_meshColors; ///< Storage for m_meshes' slot colours.
    std::map<std::string, std::shared_ptr<const scene::TriangleMesh>, std::less<>> m_meshCache;
    float m_viewportWidth = 0.0f;
    float m_viewportHeight = 0.0f;
    bool m_buildLayout = false;
    std::chrono::steady_clock::time_point m_previous;
};

} // namespace interi::editor
