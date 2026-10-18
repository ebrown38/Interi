// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{
class IFileSystem;
}

namespace interi::editor
{

/// One open scene: its objects, undo history, selection and file path. The dirty flag is the undo stack's distance
/// from the last save.
class EditorDocument
{
public:
    EditorDocument();

    [[nodiscard]] ReflectionRegistry& Reflection() noexcept { return m_reflection; }
    [[nodiscard]] const ReflectionRegistry& Reflection() const noexcept { return m_reflection; }
    [[nodiscard]] EditorScene& Scene() noexcept { return m_scene; }
    [[nodiscard]] const EditorScene& Scene() const noexcept { return m_scene; }
    [[nodiscard]] UndoStack& Undo() noexcept { return m_undo; }

    /// An empty, unnamed scene.
    void New();
    /// Opens a scene (`.scene.json`), or imports a legacy level (any other `.json`): the imported scene is named
    /// `<stem>.scene.json` next to it and the level is remembered as the export path. On failure the open document
    /// is unchanged. Warnings (unknown components, broken parents) are logged.
    [[nodiscard]] Result<void> Open(const IFileSystem& files, std::string_view path);
    /// Writes the scene to Path() (InvalidArgument if it has none) and marks it clean.
    [[nodiscard]] Result<void> Save(IFileSystem& files);
    [[nodiscard]] Result<void> SaveAs(IFileSystem& files, std::string path);
    /// Writes the scene as a level (LevelBridge.h) to `path`, or ExportPath() when empty.
    [[nodiscard]] Result<void> ExportLevel(IFileSystem& files, std::string_view path = {});
    /// Writes the scene to its autosave file (EditorServices.h AutosavePath) without marking it clean; returns the
    /// path written.
    [[nodiscard]] Result<std::string> Autosave(IFileSystem& files);
    [[nodiscard]] const std::string& Path() const noexcept { return m_path; }
    [[nodiscard]] const std::string& ExportPath() const noexcept { return m_exportPath; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_undo.IsDirty(); }

    /// The primary selection (the last selected object that still exists), or 0.
    [[nodiscard]] EditorEntityId Selection() const noexcept;
    /// Every selected object that still exists, in selection order (the primary last).
    [[nodiscard]] std::vector<EditorEntityId> SelectedIds() const;
    /// Selected objects without any whose ancestor is also selected (what group, duplicate and delete act on).
    [[nodiscard]] std::vector<EditorEntityId> SelectedRoots() const;
    [[nodiscard]] bool IsSelected(EditorEntityId id) const noexcept;
    /// Replaces the selection (0: nothing).
    void Select(EditorEntityId id);
    void Select(std::span<const EditorEntityId> ids);
    /// Adds or removes one object (ctrl-click), making it primary when added.
    void ToggleSelected(EditorEntityId id);

    /// Adds an object (undoable) and selects it. `create` makes it in the scene: e.g. EditorScene::CreatePrimitive.
    template <typename Create>
    EditorEntityId Add(Create&& create)
    {
        const EditorEntityId id = create(m_scene);
        AddCreated(id);
        return id;
    }

    /// Deletes the selected objects and their descendants as one undo step. False if nothing is selected.
    bool DeleteSelection();

private:
    void AddCreated(EditorEntityId id);

    ReflectionRegistry m_reflection;
    EditorScene m_scene;
    UndoStack m_undo;
    std::string m_path;
    std::string m_exportPath;
    std::vector<EditorEntityId> m_selection;
};

} // namespace interi::editor
