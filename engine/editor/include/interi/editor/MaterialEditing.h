// SPDX-License-Identifier: MIT
#pragma once

// Undoable edits of a PBR material (the material editor panel). An edit is checked first (`ValidateMaterial`): an
// invalid material changes nothing and the reason is returned. Edits of one field in a row (a slider drag) merge into
// one undo step until UndoStack::Seal.

#include <interi/assets/Material.h>
#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/platform/FileSystem.h>

#include <string>
#include <string_view>

namespace interi::editor
{

/// Replaces `material` with `edited` as one undo step. `field` names what changed ("roughness", "baseColor"...): edits
/// with the same non-empty field merge. InvalidArgument (ValidateMaterial's reason) leaves `material` unchanged.
Result<void> EditMaterial(UndoStack& undo, assets::PbrMaterial& material, assets::PbrMaterial edited,
                          std::string_view field = {});

/// What an authored material should name for a cooked texture dragged from the asset browser: the source file the
/// database cooked it from (`imported/tex/a.png.itex` -> `tex/a.png`). Any other path comes back unchanged.
[[nodiscard]] std::string TextureSourceOf(std::string_view assetPath, std::string_view importedFolder = "imported");

/// Reads / writes an authored `.material.json` through the file system (canonical text).
[[nodiscard]] Result<assets::PbrMaterial> OpenMaterialFile(const IFileSystem& files, std::string_view path);
Result<void> SaveMaterialFile(IFileSystem& files, std::string_view path, const assets::PbrMaterial& material);

} // namespace interi::editor
