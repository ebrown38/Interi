// SPDX-License-Identifier: MIT
#pragma once

// The HUD designer: edit a HudDocument (widgets, their anchors, offsets, sizes, bindings and actions) next to a live
// preview drawn from the same draw list a game uses.

#include <interi/editor/UndoStack.h>
#include <interi/hud/HudLayout.h>

#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

namespace interi::editor
{

inline constexpr const char* kHudDesignerWindow = "HUD Designer";

struct HudDesignerState
{
    std::string selected; ///< Widget id.
    std::string message;  ///< The last rejected edit.
    float previewWidth = 1280.0f;
    float previewHeight = 720.0f;
};

/// Draws `frame` into `list` with its top-left at `origin` (rectangles, text, and a placeholder box for images, since
/// textures belong to the renderer). Any ImGui-based game HUD can use it too.
void DrawHudFrame(ImDrawList& list, ImVec2 origin, const hud::HudFrame& frame);

struct HudDesignerResult
{
    bool edited = false;
    /// Values the document reads that `knownValues` doesn't define (shown as warnings).
    std::vector<std::string> unknownValues;
};

/// The designer window: widget list (add, duplicate, remove, reorder), the selected widget's fields and a preview.
/// Edits are undo steps and are checked before they apply. `values` supplies the preview's numbers and text;
/// `knownValues` are the ids the project defines.
HudDesignerResult DrawHudDesignerPanel(hud::HudDocument& document, UndoStack& undo, HudDesignerState& state,
                                       const chain::IValueReader& values, const std::vector<std::string>& knownValues);

} // namespace interi::editor
