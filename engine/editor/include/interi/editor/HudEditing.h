// SPDX-License-Identifier: MIT
#pragma once

// Undoable edits of a HudDocument (the HUD designer panel and menus). Every edit is checked first: the edited
// document must still parse (`ValidateHud`), otherwise nothing changes and the reason is returned. Edits of one
// widget in a row (a drag) merge into one undo step until UndoStack::Seal.

#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/hud/HudDocument.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace interi::editor
{

/// Whether `document` would load: ParseHud(WriteHud(document)). InvalidArgument with the reason.
[[nodiscard]] Result<void> ValidateHud(const hud::HudDocument& document);

/// A new widget of `type` at the end, with a unique id ("text1", "bar2", ...) and sensible defaults (a bar binds
/// "value", a button's action is "menu.pop", an image needs a path). Returns its id.
[[nodiscard]] std::string AddHudWidget(UndoStack& undo, hud::HudDocument& document, hud::WidgetType type);

/// Replaces the widget called `id` with `edited` (which may rename it). NotFound / InvalidArgument (a duplicate id or
/// a widget that wouldn't load); the document is unchanged on failure.
Result<void> EditHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id, hud::HudWidget edited);

Result<void> RemoveHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id);

/// Moves a widget by `delta` places in draw/focus order (clamped to the ends).
Result<void> MoveHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id, int delta);

/// A copy of a widget under a new unique id, right after it. Returns the new id.
Result<std::string> DuplicateHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id);

} // namespace interi::editor
