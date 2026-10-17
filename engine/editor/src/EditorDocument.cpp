// SPDX-License-Identifier: MIT
#include <interi/editor/Commands.h>
#include <interi/editor/EditorDocument.h>
#include <interi/platform/FileSystem.h>

#include <memory>

namespace interi::editor
{

EditorDocument::EditorDocument()
    : m_scene(m_reflection)
{
    RegisterEngineReflection(m_reflection);
}

void EditorDocument::New()
{
    m_scene.Clear();
    m_undo.Clear();
    m_path.clear();
    m_selection = 0;
}

Result<void> EditorDocument::Open(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(level, gameplay::LoadLevel(files, path));
    m_scene.Load(level);
    m_undo.Clear();
    m_path = std::string(path);
    m_selection = 0;
    return {};
}

Result<void> EditorDocument::Save(IFileSystem& files)
{
    if (m_path.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "the level has no file name yet");
    }
    const std::string json = gameplay::WriteLevel(m_scene.ToLevel());
    INTERI_TRY(files.WriteFile(m_path, std::as_bytes(std::span(json))));
    m_undo.MarkClean();
    m_undo.Seal();
    return {};
}

Result<void> EditorDocument::SaveAs(IFileSystem& files, std::string path)
{
    std::string previous = std::exchange(m_path, std::move(path));
    auto saved = Save(files);
    if (!saved)
    {
        m_path = std::move(previous);
    }
    return saved;
}

EditorEntityId EditorDocument::Selection() const noexcept
{
    return m_scene.Contains(m_selection) ? m_selection : 0;
}

void EditorDocument::AddCreated(EditorEntityId id)
{
    auto snapshot = m_scene.Snapshot(id);
    if (!snapshot)
    {
        return;
    }
    (void)m_scene.Destroy(id);
    m_undo.Execute(std::make_unique<CreateEntityCommand>(m_scene, std::move(*snapshot)));
    m_selection = id;
}

bool EditorDocument::DeleteSelection()
{
    const EditorEntityId id = Selection();
    if (id == 0)
    {
        return false;
    }
    m_undo.Execute(std::make_unique<DestroyEntityCommand>(m_scene, id));
    m_selection = 0;
    return true;
}

} // namespace interi::editor
