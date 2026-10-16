// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/AssetBrowser.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Gizmo.h>

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
    bool quit = false;
};

/// The windows of the default layout, by name.
inline constexpr const char* kViewportWindow = "Viewport";
inline constexpr const char* kOutlinerWindow = "Outliner";
inline constexpr const char* kInspectorWindow = "Inspector";
inline constexpr const char* kAssetsWindow = "Assets";
inline constexpr const char* kToolsWindow = "Tools";

/// A dockspace over the whole window (below the menu bar). With `buildDefaultLayout` (first run, no saved layout)
/// it is split into the outliner (left), inspector and tools (right), assets (bottom) and the viewport (centre).
/// Returns the central node's rectangle. Call between ImGui::NewFrame and the panels.
ScreenRect DrawDockSpace(bool buildDefaultLayout);

/// File and Edit menus (Undo/Redo carried out directly).
[[nodiscard]] MenuRequests DrawMainMenu(EditorDocument& document);

/// Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, Ctrl+S save, Delete removes the selection. Ignored while a text field
/// has the keyboard.
void HandleShortcuts(EditorDocument& document, MenuRequests& requests);

/// The level's entities; clicking one selects it.
void DrawOutliner(EditorDocument& document);

/// Gizmo mode and snapping.
struct ToolSettings
{
    GizmoMode mode = GizmoMode::Translate;
    bool snap = true;
    float moveSnap = 0.25f;          ///< Metres.
    float rotateSnapDegrees = 15.0f; ///< Degrees.
    float sizeSnap = 0.1f;           ///< Metres of half extent.
};

struct ToolRequests
{
    bool addStaticBox = false;
    bool addSpawnPoint = false;
    bool focusSelection = false;
};

/// Gizmo mode (W/E/R while the viewport has the mouse), snapping and buttons that add entities.
[[nodiscard]] ToolRequests DrawToolsPanel(ToolSettings& settings, const EditorDocument& document);

struct AssetRequests
{
    std::optional<std::string> openLevel;
    bool refresh = false;
};

/// The data directory's assets. Destructibles and glTF sources drag into the viewport (payload kAssetPayload);
/// double-clicking a level opens it.
[[nodiscard]] AssetRequests DrawAssetsPanel(std::span<const AssetEntry> assets);

/// The selected entity's reflected components, editable. Every edit is an undo step; drags and text edits are one
/// step each (sealed when the widget is released).
void DrawInspector(EditorDocument& document);

} // namespace interi::editor
