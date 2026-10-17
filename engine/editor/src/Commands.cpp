// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/editor/Commands.h>

#include <format>

namespace interi::editor
{

SetComponentCommand::SetComponentCommand(EditorScene& scene, EditorEntityId id, ComponentSnapshot before,
                                         ComponentSnapshot after, std::uint64_t mergeKey)
    : m_scene(&scene)
    , m_id(id)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_mergeKey(mergeKey)
    , m_label(std::format("Edit {}", m_after.component))
{
}

void SetComponentCommand::Do()
{
    INTERI_VERIFY(m_scene->WriteComponent(m_id, m_after), "SetComponentCommand: target is gone");
}

void SetComponentCommand::Undo()
{
    INTERI_VERIFY(m_scene->WriteComponent(m_id, m_before), "SetComponentCommand: target is gone");
}

bool SetComponentCommand::MergeWith(const ICommand& next)
{
    const auto* other = dynamic_cast<const SetComponentCommand*>(&next);
    if (m_mergeKey == 0 || other == nullptr || other->m_mergeKey != m_mergeKey || other->m_id != m_id ||
        other->m_after.component != m_after.component)
    {
        return false;
    }
    m_after = other->m_after;
    return true;
}

CreateEntityCommand::CreateEntityCommand(EditorScene& scene, EntitySnapshot snapshot)
    : m_scene(&scene)
    , m_snapshot(std::move(snapshot))
{
}

void CreateEntityCommand::Do()
{
    INTERI_VERIFY(m_scene->Restore(m_snapshot), "CreateEntityCommand: id in use");
}

void CreateEntityCommand::Undo()
{
    INTERI_VERIFY(m_scene->Destroy(m_snapshot.id), "CreateEntityCommand: entity is gone");
}

DestroyEntityCommand::DestroyEntityCommand(EditorScene& scene, EditorEntityId id)
    : m_scene(&scene)
    , m_snapshot{.id = id, .components = {}}
{
}

void DestroyEntityCommand::Do()
{
    auto snapshot = m_scene->Snapshot(m_snapshot.id);
    if (!snapshot.has_value())
    {
        INTERI_ASSERT(false, "DestroyEntityCommand: entity is gone");
        return;
    }
    m_snapshot = std::move(*snapshot);
    (void)m_scene->Destroy(m_snapshot.id);
}

void DestroyEntityCommand::Undo()
{
    INTERI_VERIFY(m_scene->Restore(m_snapshot), "DestroyEntityCommand: id in use");
}

} // namespace interi::editor
