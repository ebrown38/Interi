// SPDX-License-Identifier: MIT
#pragma once

// The Chain Reaction editor's model, without UI: undoable edits of a chain::ChainAsset (nodes, links, conditions,
// input bindings, comments, groups, copy and paste), the compile diagnostics mapped onto nodes and links, and the
// event palette with its override indicators. The canvas (editor_ui/ChainEditor.h) only draws this and calls it.
//
// Every edit that could break typing (adding a link, setting a condition or a binding) is checked by compiling the
// asset before and after and refusing the edit when it introduces an error; the compiler's own type rules decide
// (int promotes to float, float does not narrow to int). A half-built chain has errors of its own, so only the
// errors an edit adds count. Nodes that no link reaches are compiled as if a Start trigger led to them, so an input
// is checked before the node is linked. Refused edits change nothing and return the reason. Edits of one thing in a
// row (a drag, typing into a field) merge into one undo step until UndoStack::Seal.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Compiler.h>
#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/values/ValueRegistry.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// What chains compile against while editing. `values` may be nullptr.
struct ChainEditContext
{
    const chain::ChainRegistry* registry = nullptr;
    const values::ValueRegistry* values = nullptr;
};

// ---- Validation (22.5) -------------------------------------------------------------------------------------------

/// The compile diagnostics of `asset` (errors and warnings such as unreachable nodes), as the compiler reports them.
[[nodiscard]] std::vector<chain::Diagnostic> ValidateChain(const chain::ChainAsset& asset, const ChainEditContext& context);
/// Diagnostics located at node `id` (including its inputs and the conditions of links leaving it).
[[nodiscard]] std::vector<const chain::Diagnostic*> DiagnosticsForNode(const std::vector<chain::Diagnostic>& all,
                                                                       std::string_view id);
/// Diagnostics located at link `index` of the asset.
[[nodiscard]] std::vector<const chain::Diagnostic*> DiagnosticsForLink(const std::vector<chain::Diagnostic>& all,
                                                                       std::size_t index);
[[nodiscard]] bool HasErrors(const std::vector<chain::Diagnostic>& diagnostics) noexcept;

// ---- Palette (22.2) ----------------------------------------------------------------------------------------------

struct PaletteEntry
{
    std::string id;       ///< Event id.
    std::string module;   ///< Group: the winning provider's id ("engine" for the engine's own).
    std::string category; ///< Sub-group within the module.
    std::string tooltip;
    chain::EventKind kind = chain::EventKind::Action;
    std::vector<std::string> shadowed; ///< Providers this id overrides (empty: none).
    /// "provided by A, overrides B and C" when `shadowed` isn't empty, else empty.
    std::string indicator;
};

/// Every event of every loaded module, sorted by (module, category, id).
[[nodiscard]] std::vector<PaletteEntry> BuildPalette(const chain::ChainRegistry& registry);
/// The entries whose id, module, category or tooltip contains every word of `query` (case-insensitive).
[[nodiscard]] std::vector<const PaletteEntry*> FilterPalette(const std::vector<PaletteEntry>& palette,
                                                             std::string_view query);

// ---- Editing (22.3) ----------------------------------------------------------------------------------------------

/// A new node of `event` at `pos` with a unique id ("Shoot1"). InvalidArgument if the event isn't registered.
Result<std::string> AddChainNode(UndoStack& undo, chain::ChainAsset& asset, const ChainEditContext& context,
                                 std::string_view event, std::array<float, 2> pos);

/// Removes nodes with their links and group memberships.
Result<void> RemoveChainNodes(UndoStack& undo, chain::ChainAsset& asset, const std::vector<std::string>& ids);

/// Renames a node (links, groups follow). InvalidArgument for a bad or duplicate name.
Result<void> RenameChainNode(UndoStack& undo, chain::ChainAsset& asset, std::string_view id, std::string_view name);

/// Moves nodes (and comments named in `comments`) by `delta`. Merges into one step per selection until Seal.
Result<void> MoveChainNodes(UndoStack& undo, chain::ChainAsset& asset, const std::vector<std::string>& ids,
                            std::array<float, 2> delta, const std::vector<std::string>& comments = {});

/// Links `fromNode.fromOutput` (empty: the first output) to `toNode`. Refused: a missing node or output, a trigger as
/// the target, a duplicate or self link, a condition that doesn't compile as bool, or a link that breaks a binding.
Result<void> AddChainLink(UndoStack& undo, chain::ChainAsset& asset, const ChainEditContext& context,
                          std::string_view fromNode, std::string_view fromOutput, std::string_view toNode,
                          std::string_view condition = {});
Result<void> RemoveChainLink(UndoStack& undo, chain::ChainAsset& asset, std::size_t index);
/// Sets (empty: clears) a link's condition. Refused unless it compiles as bool.
Result<void> SetChainLinkCondition(UndoStack& undo, chain::ChainAsset& asset, const ChainEditContext& context,
                                   std::size_t index, std::string_view condition);

/// Binds input `input` of `node` (nullopt: back to the default). Refused when the binding doesn't type-check for the
/// input (a constant that doesn't fit, an unknown payload field or Value, an expression of the wrong type).
Result<void> BindChainInput(UndoStack& undo, chain::ChainAsset& asset, const ChainEditContext& context,
                            std::string_view node, std::string_view input, std::optional<chain::Binding> binding);

Result<void> SetChainPriority(UndoStack& undo, chain::ChainAsset& asset, std::int32_t priority);

Result<std::string> AddChainComment(UndoStack& undo, chain::ChainAsset& asset, std::array<float, 2> pos,
                                    std::string text);
Result<void> EditChainComment(UndoStack& undo, chain::ChainAsset& asset, std::string_view id,
                              chain::ChainComment edited);
Result<void> RemoveChainComment(UndoStack& undo, chain::ChainAsset& asset, std::string_view id);

/// A group around `nodes` (a node belongs to at most one group: it leaves its old one). Returns the group id.
Result<std::string> GroupChainNodes(UndoStack& undo, chain::ChainAsset& asset, const std::vector<std::string>& nodes,
                                    std::string title);
Result<void> EditChainGroup(UndoStack& undo, chain::ChainAsset& asset, std::string_view id, chain::ChainGroup edited);
Result<void> UngroupChain(UndoStack& undo, chain::ChainAsset& asset, std::string_view id);

/// Copied nodes and the links between them.
struct ChainClipboard
{
    std::vector<chain::ChainNode> nodes;
    std::vector<chain::ChainLink> links;

    [[nodiscard]] bool Empty() const noexcept { return nodes.empty(); }
};

[[nodiscard]] ChainClipboard CopyChainNodes(const chain::ChainAsset& asset, const std::vector<std::string>& ids);
/// Adds the clipboard's nodes under fresh ids, shifted by `offset`, with their internal links. Returns the new ids.
Result<std::vector<std::string>> PasteChainNodes(UndoStack& undo, chain::ChainAsset& asset,
                                                 const ChainClipboard& clipboard, std::array<float, 2> offset);

} // namespace interi::editor
