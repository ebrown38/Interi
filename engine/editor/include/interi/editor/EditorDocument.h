// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>

#include <string>
#include <string_view>

namespace interi
{
class IFileSystem;
}

namespace interi::editor
{

/// One open level: its scene, undo history, selection and file path. The dirty flag is the undo stack's distance
/// from the last save.
class EditorDocument
{
public:
    EditorDocument();

    [[nodiscard]] const ReflectionRegistry& Reflection() const noexcept { return m_reflection; }
    [[nodiscard]] EditorScene& Scene() noexcept { return m_scene; }
    [[nodiscard]] const EditorScene& Scene() const noexcept { return m_scene; }
    [[nodiscard]] UndoStack& Undo() noexcept { return m_undo; }

    /// An empty, unnamed level.
    void New();
    /// Loads a level file. On failure the open document is unchanged.
    [[nodiscard]] Result<void> Open(const IFileSystem& files, std::string_view path);
    /// Writes the level to Path() (InvalidArgument if it has none) and marks it clean.
    [[nodiscard]] Result<void> Save(IFileSystem& files);
    [[nodiscard]] Result<void> SaveAs(IFileSystem& files, std::string path);
    [[nodiscard]] const std::string& Path() const noexcept { return m_path; }
    [[nodiscard]] bool IsDirty() const noexcept { return m_undo.IsDirty(); }

    /// The selected entity, or 0 (none, or it no longer exists).
    [[nodiscard]] EditorEntityId Selection() const noexcept;
    void Select(EditorEntityId id) noexcept { m_selection = id; }

    /// Adds an entity (undoable) and selects it. `create` makes it in the scene: e.g. EditorScene::CreateStaticBox.
    template <typename Create>
    EditorEntityId Add(Create&& create)
    {
        const EditorEntityId id = create(m_scene);
        AddCreated(id);
        return id;
    }

    /// Deletes the selected entity (undoable). False if nothing is selected.
    bool DeleteSelection();

private:
    void AddCreated(EditorEntityId id);

    ReflectionRegistry m_reflection;
    EditorScene m_scene;
    UndoStack m_undo;
    std::string m_path;
    EditorEntityId m_selection = 0;
};

} // namespace interi::editor
