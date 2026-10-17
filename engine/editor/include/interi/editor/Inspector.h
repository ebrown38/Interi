// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/EditorScene.h>
#include <interi/editor/UndoStack.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// Records an edit of one reflected field as an undoable SetComponentCommand (applied immediately). Returns false and
/// records nothing if the entity, component or field doesn't exist, the value has the wrong type, or it is
/// unchanged. Edits with the same non-zero `mergeKey` merge until the stack is sealed (UI: the widget's id while it
/// is dragged).
bool EditField(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component,
               std::string_view field, const FieldValue& value, std::uint64_t mergeKey = 0);

/// Replaces an entity's Transform as one undoable command (gizmos). False if unchanged or the entity is gone.
bool EditTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& transform,
                   std::uint64_t mergeKey = 0);

/// One line of the outliner.
struct OutlinerRow
{
    EditorEntityId id = 0;
    EntityKind kind = EntityKind::None;
    std::string label; ///< e.g. "Static box 3", "Destructible 4 (generated/wall.idest)", "Spawn 7 (player)".
};

/// The outliner's rows, by id.
[[nodiscard]] std::vector<OutlinerRow> OutlinerRows(const EditorScene& scene);

} // namespace interi::editor
