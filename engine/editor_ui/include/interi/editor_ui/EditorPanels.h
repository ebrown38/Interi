// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/AssetBrowser.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Modeling.h>
#include <interi/scene/Primitives.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace interi::editor
{

/// A rectangle of the window in pixels.
struct ScreenRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool Contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

/// What the user asked for this frame through the menu or shortcuts (the app carries them out).
struct MenuRequests
{
    bool newLevel = false;
    bool save = false;
    bool exportLevel = false; ///< Write the scene as a level to the document's export path.
    bool group = false;       ///< Ctrl+G (the app groups: it knows asset bounds).
    bool quit = false;
};

/// The windows of the default layout, by name.
inline constexpr const char* kViewportWindow = "Viewport";
inline constexpr const char* kOutlinerWindow = "Outliner";
inline constexpr const char* kInspectorWindow = "Inspector";
inline constexpr const char* kAssetsWindow = "Assets";
inline constexpr const char* kToolsWindow = "Tools";
inline constexpr const char* kFractureWindow = "Fracture";   ///< FracturePanel.h
inline constexpr const char* kStructureWindow = "Structure"; ///< Stress and replay tools.

/// A dockspace over the whole window (below the menu bar). With `buildDefaultLayout` (first run, no saved layout)
/// it is split into the outliner (left), inspector and tools / fracture / structure / values / characters tabs
/// (right), assets and data tables (bottom) and the viewport (centre).
/// Returns the central node's rectangle. Call between ImGui::NewFrame and the panels.
ScreenRect DrawDockSpace(bool buildDefaultLayout);

/// File and Edit menus (Undo/Redo and Delete carried out directly).
[[nodiscard]] MenuRequests DrawMainMenu(EditorDocument& document);

/// Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, Ctrl+S save, Ctrl+D duplicates and Delete removes the selection, Ctrl+G
/// requests a group. Ignored while a text field has the keyboard.
void HandleShortcuts(EditorDocument& document, MenuRequests& requests);

/// The scene's object tree: click selects, ctrl-click adds to the selection, dragging a row onto another reparents
/// it (onto the empty space below the tree: makes it a root).
void DrawOutliner(EditorDocument& document);

/// Gizmo mode and snapping.
struct ToolSettings
{
    GizmoMode mode = GizmoMode::Translate;
    bool snap = true;
    float moveSnap = 0.25f;          ///< Metres.
    float rotateSnapDegrees = 15.0f; ///< Degrees.
    float sizeSnap = 0.1f;           ///< Scale factor step.
    int axis = 0;                    ///< Align, distribute and mirror axis (0 = x, 1 = y, 2 = z).
    int arrayCount = 3;
    Vec3 arrayOffset{2.0f, 0.0f, 0.0f};
};

struct ToolRequests
{
    std::optional<scene::PrimitiveShape> addPrimitive;
    bool addSpawnPoint = false;
    // Modeling on the selection (Modeling.h), carried out by the app (it knows asset bounds and files).
    bool group = false;
    bool ungroup = false;
    bool duplicate = false;
    bool array = false;
    std::optional<AlignMode> align;
    bool distribute = false;
    bool mirror = false;
    bool snapSelection = false;
    std::optional<PivotPlacement> pivot;
    std::optional<modeling::CsgOperation> csg;
    bool createPrefab = false; ///< From the selected object (prefabs/<name>.prefab.json).
    bool revertPrefab = false; ///< The selected object's instance.
    bool applyPrefab = false;
    bool focusSelection = false;
};

/// Gizmo mode (W/E/R while the viewport has the mouse), snapping and buttons that add primitives and spawn points.
[[nodiscard]] ToolRequests DrawToolsPanel(ToolSettings& settings, const EditorDocument& document);

struct AssetRequests
{
    std::optional<std::string> openLevel;
    bool refresh = false;
};

/// The data directory's assets. Destructibles, meshes and glTF sources drag into the viewport (payload
/// kAssetPayload); double-clicking a scene or a level opens it.
[[nodiscard]] AssetRequests DrawAssetsPanel(std::span<const AssetEntry> assets);

struct ScenePlayStatus
{
    bool playing = false;
    bool paused = false;
    std::uint64_t step = 0;
};

struct ScenePlayRequests
{
    bool play = false;
    bool pause = false; ///< Toggles.
    bool step = false;
    bool stop = false;
};

/// Play / Pause / Step / Stop buttons and the step counter, in the current window (the viewport's top). F5 plays or
/// stops, F6 pauses, F10 steps, while no text field has the keyboard.
[[nodiscard]] ScenePlayRequests DrawPlayControls(const ScenePlayStatus& status);

/// The selected object's reflected components, editable, plus Add / Remove component. Every edit is an undo step;
/// drags and text edits are one step each (sealed when the widget is released).
void DrawInspector(EditorDocument& document);

} // namespace interi::editor
