// SPDX-License-Identifier: MIT
#pragma once

// Panels for imported assets: the material editor (edits an authored `.material.json` next to a live preview sphere)
// and the import panel (the import settings of the selected asset's source file, with re-import), plus the helper that
// draws a CPU image into an ImGui draw list.

#include <interi/asset_import/AssetDatabase.h>
#include <interi/assets/Material.h>
#include <interi/assets/Texture.h>
#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/platform/FileSystem.h>

#include <imgui.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace interi::editor
{

inline constexpr const char* kMaterialEditorWindow = "Material Editor";
inline constexpr const char* kImportWindow = "Import";

/// Draws an RGBA8 image with its top-left at `origin`, `cell` screen pixels per texel, as filled rectangles (runs of
/// equal texels merge). Texels with alpha 0 are skipped. For previews and thumbnails: no GPU texture needed.
void DrawPixels(ImDrawList& list, ImVec2 origin, float cell, std::span<const std::uint8_t> rgba, std::uint32_t width,
                std::uint32_t height);

struct MaterialEditorState
{
    std::string path; ///< The open file; empty: nothing open.
    assets::PbrMaterial material;
    bool readOnly = false; ///< A cooked `.imat`: shown, not saved.
    UndoStack undo;
    std::string message; ///< The last rejected edit or save result.
    assets::Image8 preview;
    bool previewStale = true;
    std::uint32_t previewSize = 112;
};

/// Opens a `.material.json` (editable) or a cooked `.imat` (read-only) in the editor and clears its undo history.
Result<void> OpenMaterialInEditor(MaterialEditorState& state, const IFileSystem& files, std::string path);

struct MaterialEditorResult
{
    bool edited = false;
    bool saved = false;
};

/// The material editor window: every factor and texture slot, the preview sphere (re-rendered when the material
/// changes), undo / redo and Save. Texture slots accept cooked textures dragged from the asset browser. Edits are
/// undo steps and are checked before they apply.
MaterialEditorResult DrawMaterialEditorPanel(MaterialEditorState& state, IFileSystem& files);

struct ImportPanelState
{
    std::string source; ///< The source file whose settings are shown (set from the selected asset).
    asset_import::ModelImportSettings model;
    asset_import::TextureImportSettings texture;
    std::string message; ///< The result of the last re-import.
};

struct ImportPanelResult
{
    bool reimported = false; ///< Settings applied and the source cooked again: refresh the asset list.
};

/// The import panel: for `selectedAsset` (a source or a cooked product of one) its guid, import settings, products,
/// dependencies and dependents, and "Re-import" which applies the settings and cooks the source.
ImportPanelResult DrawImportPanel(asset_import::AssetDatabase* database, ImportPanelState& state,
                                  std::string_view selectedAsset);

} // namespace interi::editor
