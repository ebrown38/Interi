// SPDX-License-Identifier: MIT
#pragma once

// Prefab editing (scene/Prefab.h has the format). Prefabs are read from and written to `files` by virtual path.

#include <interi/core/Result.h>
#include <interi/editor/EditorDocument.h>
#include <interi/scene/Prefab.h>

#include <string>

namespace interi
{
class IFileSystem;
}

namespace interi::editor
{

/// Reads prefabs from a file system (each checked with scene::PrefabRoot).
[[nodiscard]] scene::PrefabLoader PrefabLoaderFor(const IFileSystem& files);

/// The instance root an object belongs to (itself for a root), or 0 if it isn't part of an instance.
[[nodiscard]] EditorEntityId InstanceRootOf(const EditorScene& scene, EditorEntityId id);

/// Writes the single selected root and its subtree as a prefab to `path` and turns it into an instance of it (one
/// undo step; the file stays). InvalidArgument unless exactly one root is selected.
[[nodiscard]] Result<void> CreatePrefabFromSelection(EditorDocument& document, IFileSystem& files,
                                                     const std::string& path);

/// Adds an instance of a prefab at a world transform (one undo step) and selects it.
[[nodiscard]] Result<EditorEntityId> InstantiatePrefab(EditorDocument& document, const IFileSystem& files,
                                                       std::string path, const Transform& world);

/// Adds a cooked model (its `manifest.imodel.json`, see asset_import) at a world transform as one undo step and
/// selects it: a root object with a child per mesh. Each child shows the cooked `.imesh` and takes its slot colours
/// from the base colour of the slot's cooked material (grey when the slot has none). NotFound / Corrupt /
/// InvalidArgument for a manifest that can't be read.
[[nodiscard]] Result<EditorEntityId> InstantiateModel(EditorDocument& document, const IFileSystem& files,
                                                      std::string manifestPath, const Transform& world);

/// Puts an instance back to its prefab: every override is dropped (one undo step). Objects added under it stay.
[[nodiscard]] Result<void> RevertInstance(EditorDocument& document, const IFileSystem& files, EditorEntityId root);

/// Writes an instance's current state into its prefab, then reloads the scene so every instance shows it (other
/// instances keep their own overrides). Clears the undo history, like reopening the scene.
[[nodiscard]] Result<void> ApplyInstance(EditorDocument& document, IFileSystem& files, EditorEntityId root);

} // namespace interi::editor
