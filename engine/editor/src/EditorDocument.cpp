// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/editor/Commands.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorServices.h>
#include <interi/editor/Prefabs.h>
#include <interi/platform/FileSystem.h>

#include <algorithm>
#include <format>
#include <memory>
#include <ranges>

namespace interi::editor
{

namespace
{

constexpr std::string_view kSceneExtension = ".scene.json";

[[nodiscard]] bool IsScenePath(std::string_view path) noexcept
{
    return path.ends_with(kSceneExtension);
}

} // namespace

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
    m_selection.clear();
}

Result<void> EditorDocument::Open(const IFileSystem& files, std::string_view path)
{
    if (!IsScenePath(path))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("'{}' is not a scene ({})", path, kSceneExtension));
    }
    INTERI_TRY_ASSIGN(document, scene::LoadScene(files, path));
    scene::ExpandPrefabs(document, PrefabLoaderFor(files), document.warnings);
    INTERI_TRY_ASSIGN(warnings, m_scene.Load(document));
    for (const std::string& warning : document.warnings)
    {
        INTERI_LOG_WARN("Editor", "{}: {}", path, warning);
    }
    for (const std::string& warning : warnings)
    {
        INTERI_LOG_WARN("Editor", "{}: {}", path, warning);
    }
    m_path = std::string(path);
    m_undo.Clear();
    m_selection.clear();
    return {};
}

Result<void> EditorDocument::Save(IFileSystem& files)
{
    if (m_path.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "the scene has no file name yet");
    }
    scene::SceneDocument document = m_scene.ToDocument();
    // Instances save as their root and overrides.
    scene::CollapsePrefabs(document, PrefabLoaderFor(files), m_reflection);
    const std::string json = scene::WriteScene(document);
    INTERI_TRY(files.WriteFile(m_path, std::as_bytes(std::span(json))));
    m_undo.MarkClean();
    m_undo.Seal();
    return {};
}

Result<std::string> EditorDocument::Autosave(IFileSystem& files)
{
    scene::SceneDocument document = m_scene.ToDocument();
    scene::CollapsePrefabs(document, PrefabLoaderFor(files), m_reflection);
    const std::string json = scene::WriteScene(document);
    std::string path = AutosavePath(m_path);
    INTERI_TRY(files.WriteFile(path, std::as_bytes(std::span(json))));
    return path;
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
    for (const EditorEntityId id : std::views::reverse(m_selection))
    {
        if (m_scene.Contains(id))
        {
            return id;
        }
    }
    return 0;
}

std::vector<EditorEntityId> EditorDocument::SelectedIds() const
{
    std::vector<EditorEntityId> ids;
    for (const EditorEntityId id : m_selection)
    {
        if (m_scene.Contains(id))
        {
            ids.push_back(id);
        }
    }
    return ids;
}

std::vector<EditorEntityId> EditorDocument::SelectedRoots() const
{
    const std::vector<EditorEntityId> selected = SelectedIds();
    std::vector<EditorEntityId> roots;
    for (const EditorEntityId id : selected)
    {
        const bool covered = std::ranges::any_of(selected, [&](EditorEntityId other)
                                                 { return other != id && m_scene.Hierarchy().IsWithin(id, other); });
        if (!covered)
        {
            roots.push_back(id);
        }
    }
    return roots;
}

bool EditorDocument::IsSelected(EditorEntityId id) const noexcept
{
    return id != 0 && m_scene.Contains(id) && std::ranges::contains(m_selection, id);
}

void EditorDocument::Select(EditorEntityId id)
{
    m_selection.clear();
    if (id != 0)
    {
        m_selection.push_back(id);
    }
}

void EditorDocument::Select(std::span<const EditorEntityId> ids)
{
    m_selection.assign(ids.begin(), ids.end());
}

void EditorDocument::ToggleSelected(EditorEntityId id)
{
    if (const auto found = std::ranges::find(m_selection, id); found != m_selection.end())
    {
        m_selection.erase(found);
    }
    else if (id != 0)
    {
        m_selection.push_back(id);
    }
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
    Select(id);
}

bool EditorDocument::DeleteSelection()
{
    const std::vector<EditorEntityId> roots = SelectedRoots();
    if (roots.empty())
    {
        return false;
    }
    std::vector<std::unique_ptr<ICommand>> commands;
    for (const EditorEntityId root : roots)
    {
        for (const EditorEntityId id : std::views::reverse(m_scene.Subtree(root))) // children first
        {
            commands.push_back(std::make_unique<DestroyEntityCommand>(m_scene, id));
        }
    }
    m_undo.Execute(std::make_unique<MacroCommand>("Delete", std::move(commands)));
    m_selection.clear();
    return true;
}

} // namespace interi::editor
