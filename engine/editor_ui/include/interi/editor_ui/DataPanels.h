// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/UndoStack.h>
#include <interi/values/DataRegistry.h>

#include <string>

namespace interi::editor
{

inline constexpr const char* kValuesWindow = "Values";
inline constexpr const char* kCharactersWindow = "Characters";
inline constexpr const char* kDataTablesWindow = "Data Tables";

/// What the data panels remember between frames.
struct DataPanelState
{
    std::string archetype; ///< Selected in the Characters panel.
    std::string table;     ///< Selected in the Data Tables panel.
    std::string newRow;    ///< The row id being typed.
    std::string message;   ///< The last rejected edit, shown until the next one succeeds.
};

/// The project's Values by category: each with a provenance badge (who set it; the tooltip lists every provider),
/// an editor for the effective value and a reset button for a project override. Edits are undo steps (drags merge
/// until the widget is released).
void DrawValuesPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state);

/// An archetype's attributes and their defaults (project overrides with a badge and a reset button).
void DrawCharactersPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state);

/// A data table's rows as a grid: one column per table column, rows added and removed by id.
void DrawDataTablesPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state);

} // namespace interi::editor
