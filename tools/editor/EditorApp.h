// SPDX-License-Identifier: MIT
#pragma once

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/ChainDebugger.h>
#include <interi/editor/DataEditing.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorModules.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>
#include <interi/editor/ScenePlay.h>
#include <interi/editor_ui/ChainEditor.h>
#include <interi/editor_ui/DataPanels.h>
#include <interi/editor_ui/EditorExtensions.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/HudDesigner.h>
#include <interi/editor_ui/ImGuiInput.h>
#include <interi/editor_ui/ServicePanels.h>
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
    /// Where preferences are kept; empty: DefaultPreferencesPath().
    std::filesystem::path preferencesPath;
    /// Folders searched for a project module's library after its own folder (a build output directory). A project
    /// whose modules can't load falls back to the core chain library.
    std::vector<std::filesystem::path> moduleLibraries;
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
    /// Opens the Chain Editor on a chain asset (errors show in its file bar).
    void OpenChain(const std::string& path);
    /// Starts play-in-editor of the open scene (smoke runs: --play 1).
    void PlayScene() { HandlePlay({.play = true, .pause = false, .step = false, .stop = false}, 0.0f); }
    [[nodiscard]] std::uint64_t ScenePlaySteps() const noexcept
    {
        return m_scenePlay != nullptr ? m_scenePlay->StepIndex() : 0;
    }
    /// Opens a project folder (project.json): see OpenProjectFolder.
    void OpenProject(const std::filesystem::path& folder) { OpenProjectFolder(folder); }
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
    /// What modules' IEditorDocument needs besides the document.
    [[nodiscard]] DocumentHost FacadeHost();
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
    /// The project's data and chain registry: the loaded modules' when they loaded, else the module-less ones.
    [[nodiscard]] values::DataRegistry* ActiveData() noexcept;
    [[nodiscard]] chain::ChainRegistry& ActiveChains() noexcept;
    /// Loads the project's modules into a host (extensions register through it); logs and falls back on failure.
    void LoadProjectModulesHost(const sdk::Project& project);
    void DrawHud();
    void DrawChainEditorWindow();

    void HandlePlay(const ScenePlayRequests& requests, float seconds);
    /// Opens a project folder: mounts it (its paths are the project's), resolves its modules, loads its data and
    /// opens its startup scene. Errors are logged; the current project stays.
    void OpenProjectFolder(const std::filesystem::path& folder);
    void HandleProjectRequests(const ProjectRequests& requests);
    void HandleProjectSettings(const ProjectSettingsRequests& requests);
    /// Saves and reopens the project `changed`, or logs and shows (Project menu) why it was refused.
    void ApplyProjectChange(Result<sdk::Project> changed, const std::string& what);
    /// Panels for the editor services, autosave and profiler frame marks.
    void UpdateServices(float seconds);
    /// Carries out the Tools panel's modeling requests on the selection.
    void HandleModeling(const ToolRequests& tools, bool groupShortcut);
    /// The mesh an object draws (its primitive's, cached by parameters, or its mesh asset's); nullptr if none.
    [[nodiscard]] std::shared_ptr<const scene::TriangleMesh> ObjectMesh(const World& world, Entity entity);

    SdlPlatform& m_platform;
    IWindow& m_window;
    JobSystem& m_jobs;
    std::filesystem::path m_dataDir;
    NativeFileSystem m_dataFiles;
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
    /// Play-in-editor of the scene (chains, physics-free).
    std::unique_ptr<ScenePlaySession> m_scenePlay;
    bool m_keepPlayValues = true;
    ScenePlayRequests m_playRequests; ///< From this frame's viewport.
    std::size_t m_playUndoMark = 0;   ///< UndoStack::Applied() when play started.
    Preferences m_preferences;
    std::filesystem::path m_preferencesPath;
    AutosaveTimer m_autosave{std::chrono::seconds(120)};
    std::shared_ptr<LogBuffer> m_log = std::make_shared<LogBuffer>();
    ConsoleState m_console;
    ProjectMenuState m_projectMenu;
    std::optional<sdk::Project> m_projectDef; ///< The open project folder's project.json.
    std::vector<sdk::ModuleManifest> m_projectManifests;
    PhysicsEngineStatus m_physicsEngine;
    std::vector<std::string> m_addableModules; ///< Installed modules the Project menu can add.
    std::vector<ProjectTemplate> m_templates;
    std::unique_ptr<NativeFileSystem> m_projectFiles; ///< The open project folder, mounted over the data tree.
    sdk::ModuleSet m_chainModules;
    std::unique_ptr<chain::ChainRegistry> m_chains;
    std::vector<std::filesystem::path> m_moduleLibraries;
    /// What modules add to the editor. Declared before m_modules: the host takes the entries back while unloading.
    EditorExtensions m_extensions{m_document.Reflection()};
    std::unique_ptr<EditorModules> m_modules; ///< The open project's loaded modules (null: none, or they didn't load).
    struct HudState
    {
        bool show = false;
        std::string path = "content/ui/hud.hud.json";
        hud::HudDocument document;
        UndoStack undo;
        HudDesignerState ui;
        std::string message;
    };
    HudState m_hud;
    /// The chain editor (22): the open .chain.json, its undo history, canvas state and the live debugger.
    struct ChainState
    {
        bool show = false;
        std::string path = "content/chains/new.chain.json";
        chain::ChainAsset asset;
        UndoStack undo;
        ChainEditorState ui;
        ChainDebugger debugger;
        std::string message;
        std::uint64_t revision = 0; ///< Bumped when the chain registry changes (rebuilds the palette).
    };
    ChainState m_chain;
    std::vector<EditorBox> m_boxes;
    std::vector<EditorMesh> m_meshes;
    std::vector<std::vector<Vec3>> m_meshColors; ///< Storage for m_meshes' slot colours.
    std::map<std::string, std::shared_ptr<const scene::TriangleMesh>, std::less<>> m_meshCache;
    float m_viewportWidth = 0.0f;
    float m_viewportHeight = 0.0f;
    bool m_buildLayout = false;
    std::chrono::steady_clock::time_point m_previous;
};

} // namespace interi::editor
