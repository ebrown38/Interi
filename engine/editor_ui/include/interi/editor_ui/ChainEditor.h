// SPDX-License-Identifier: MIT
#pragma once

// The Chain Reaction visual editor: a node canvas (pan with the middle mouse button, zoom with the wheel, minimap)
// where chains are drawn as links between nodes' output pins and the next node, conditional links dashed with an "if"
// badge; the event palette (grouped by module, searchable, with "provided by A, overrides B" indicators); an
// inspector with the condition editor and type-filtered binding pickers; inline compile problems; and the live
// debugger (firing nodes and links light up during play, breakpoints, step, watches, a timeline).
//
// All state changes go through the headless model (ChainEditing.h, ChainDebugger.h): this file draws it and turns
// input into calls. Everything drawn here uses ImDrawList only, so there is no dependency beyond ImGui.
//
// Canvas input: left drag on a node header moves the selection, left drag from an output pin makes a link (drop it on
// a node), left drag on empty space selects a box, right click opens the add-node or node menu, Delete removes the
// selection, Ctrl+C / Ctrl+V copy and paste, Ctrl+G groups. Drag an event from the palette onto the canvas to add it.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Compiler.h>
#include <interi/editor/ChainDebugger.h>
#include <interi/editor/ChainEditing.h>
#include <interi/editor/UndoStack.h>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace interi::editor
{

inline constexpr const char* kChainEditorWindow = "Chain Editor";

/// A rectangle in canvas units (before pan and zoom).
struct ChainRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool Contains(float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

inline constexpr float kChainNodeWidth = 190.0f;
inline constexpr float kChainNodeHeader = 24.0f;
inline constexpr float kChainNodeRow = 20.0f;

/// A node's rectangle: its position, a fixed width, and a header plus a row per input or output (whichever has more).
/// `definition` may be nullptr (unknown event: one row).
[[nodiscard]] ChainRect ChainNodeRect(const chain::ChainNode& node, const chain::EventDefinition* definition);
/// Where output `index`'s pin sits (right edge), and the node's input pin (left edge, in the header).
[[nodiscard]] std::array<float, 2> ChainOutputPin(const ChainRect& rect, std::size_t index);
[[nodiscard]] std::array<float, 2> ChainInputPin(const ChainRect& rect);
/// The link's curve: a cubic bezier from `from` to `to`, sampled into `segments + 1` points.
[[nodiscard]] std::vector<std::array<float, 2>> ChainLinkCurve(std::array<float, 2> from, std::array<float, 2> to,
                                                               int segments = 24);
/// The distance from `point` to the curve (for picking links).
[[nodiscard]] float ChainDistanceToLink(std::array<float, 2> from, std::array<float, 2> to, std::array<float, 2> point);

struct ChainEditorState
{
    // View.
    std::array<float, 2> pan = {40.0f, 40.0f};
    float zoom = 1.0f;
    std::array<float, 2> canvasOrigin = {0.0f, 0.0f}; ///< Screen position of the canvas (set each frame).
    bool showMinimap = true;
    // Selection.
    std::set<std::string> selected; ///< Node ids.
    int selectedLink = -1;
    std::string selectedComment;
    // Interaction.
    bool draggingNodes = false;
    bool draggingComment = false;
    bool boxSelecting = false;
    std::array<float, 2> boxStart = {0.0f, 0.0f}; ///< Canvas units.
    std::string linkFromNode;                     ///< A link drag in progress.
    std::string linkFromOutput;
    std::array<float, 2> contextPos = {0.0f, 0.0f};
    std::string contextNode;
    // Palette, clipboard and messages.
    std::string paletteQuery;
    ChainClipboard clipboard;
    std::string message;                              ///< The last refused edit.
    std::map<std::string, std::string> bindingErrors; ///< "<node>.<input>" -> why the last edit was refused.
    std::string watchInput;
    // Caches (rebuilt when the asset or the registry revision changes).
    chain::ChainAsset cachedAsset;
    bool cacheValid = false;
    std::vector<chain::Diagnostic> diagnostics;
    std::vector<PaletteEntry> palette;
    std::uint64_t paletteRevision = ~std::uint64_t{0};
};

/// What the panel reads.
struct ChainEditorHost
{
    ChainEditContext context;
    /// Changes whenever the registries do (modules reloaded): rebuilds the palette.
    std::uint64_t revision = 0;
    ChainDebugger* debugger = nullptr; ///< nullptr: no debugger UI.
    bool playing = false;
    bool paused = false;
    std::uint64_t playStep = 0;
    const chain::IValueReader* values = nullptr; ///< For the watch panel.
};

struct ChainEditorResult
{
    bool edited = false;
    bool stepRequested = false;   ///< The Step button: run one fixed step.
    bool resumeRequested = false; ///< Continue after a breakpoint.
    bool pauseRequested = false;
    /// The palette's "Create Plugin override..." was chosen for this event id (open the Module Manager's Plugin tab).
    std::optional<std::string> overrideEvent;
};

/// The editor window (palette, canvas, inspector, problems and debugger tabs).
ChainEditorResult DrawChainEditor(chain::ChainAsset& asset, UndoStack& undo, ChainEditorState& state,
                                  const ChainEditorHost& host);

} // namespace interi::editor
