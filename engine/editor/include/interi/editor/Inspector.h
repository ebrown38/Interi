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
/// records nothing if the object, component or field doesn't exist, the value has the wrong type, or it is
/// unchanged. Edits with the same non-zero `mergeKey` merge until the stack is sealed (UI: the widget's id while it
/// is dragged).
bool EditField(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component,
               std::string_view field, const FieldValue& value, std::uint64_t mergeKey = 0);

/// Moves an object to a world transform (gizmos): its local transform becomes the parent's inverse times `world`.
/// One undoable command; false if unchanged or the object is gone.
bool EditTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& world,
                   std::uint64_t mergeKey = 0);
/// Replaces an object's local transform (one undoable command).
bool EditLocalTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& local,
                        std::uint64_t mergeKey = 0);

/// Makes `parent` (0: none) the object's parent, keeping its world transform. False (nothing recorded) if either is
/// missing, it already is the parent, or `parent` is the object or below it.
bool Reparent(EditorScene& scene, UndoStack& undo, EditorEntityId id, EditorEntityId parent);

/// Adds a reflected component with its defaults / removes one (not a standard component), undoably.
bool AddComponent(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component);
bool RemoveComponent(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component);

/// One line of the outliner.
struct OutlinerRow
{
    EditorEntityId id = 0;
    std::uint32_t depth = 0; ///< 0 for roots.
    bool hasChildren = false;
    std::string label; ///< The object's name, or "Object <id>" if it has none.
};

/// The outliner's rows: the hierarchy in pre-order (parents first, siblings by id).
[[nodiscard]] std::vector<OutlinerRow> OutlinerRows(const EditorScene& scene);

} // namespace interi::editor
