// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/AssetBrowser.h>
#include <interi/editor/FracturePreview.h>
#include <interi/editor_ui/EditorPanels.h>

#include <optional>
#include <span>
#include <string>

namespace interi::editor
{

/// What the fracture panel shows and edits.
struct FractureTool
{
    std::string active;      ///< FracturePreview key (the glTF's path under the data directory), "" = none.
    std::string object;      ///< Node whose settings are edited; "" = the defaults.
    int levelShown = 1;      ///< Hierarchy level drawn in the preview (deeper chunks stop at their leaves).
    int levelEdited = 0;     ///< Fracture level the brush paints into.
    float explode = 0.25f;   ///< Pushes chunks apart from the asset's centre (0 = assembled).
    bool showPreview = true; ///< The viewport shows the preview instead of the level.
    bool paint = false;      ///< Left mouse paints seed density on the object.
    float brushRadius = 0.4f;
    float brushDensity = 150.0f; ///< Seeds per m^3.
};

struct FractureRequests
{
    std::optional<std::string> open; ///< A glTF to open (path under the data directory).
    bool saveAndCook = false;
};

/// Pick a glTF source, edit its fracture settings (per object or defaults: material, levels and their seed
/// patterns, painted strokes, material volumes, anchor/skip), see the preview's stats or cook error, and save the
/// sidecar and cook. Edits change the entry's config; FracturePreview::Update recooks it.
[[nodiscard]] FractureRequests DrawFracturePanel(FractureTool& tool, FracturePreview& preview,
                                                 std::span<const AssetEntry> sources);

/// The settings the panel edits for `tool.object` ("" = defaults).
[[nodiscard]] destruction::ObjectCookSettings EditedSettings(const FracturePreview::Entry& entry,
                                                             const std::string& object);
/// Stores edited settings back (`object` "" = defaults; otherwise the node's complete entry).
void StoreSettings(FracturePreview::Entry& entry, const std::string& object,
                   const destruction::ObjectCookSettings& settings);

} // namespace interi::editor
