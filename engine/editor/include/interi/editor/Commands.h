// SPDX-License-Identifier: MIT
#pragma once

#include <interi/editor/EditorScene.h>
#include <interi/editor/UndoStack.h>

#include <cstdint>
#include <string>

namespace interi::editor
{

/// Replaces one component's values. Commands with the same entity, component and non-zero merge key merge (a drag
/// or a slider becomes one undo step).
class SetComponentCommand final : public ICommand
{
public:
    SetComponentCommand(EditorScene& scene, EditorEntityId id, ComponentSnapshot before, ComponentSnapshot after,
                        std::uint64_t mergeKey = 0);

    void Do() override;
    void Undo() override;
    [[nodiscard]] std::string_view Label() const override { return m_label; }
    bool MergeWith(const ICommand& next) override;

    [[nodiscard]] EditorEntityId Target() const noexcept { return m_id; }
    [[nodiscard]] const ComponentSnapshot& After() const noexcept { return m_after; }

private:
    EditorScene* m_scene;
    EditorEntityId m_id;
    ComponentSnapshot m_before;
    ComponentSnapshot m_after;
    std::uint64_t m_mergeKey;
    std::string m_label;
};

/// Creates an entity from a snapshot (its id must be free); undo destroys it.
class CreateEntityCommand final : public ICommand
{
public:
    CreateEntityCommand(EditorScene& scene, EntitySnapshot snapshot);

    void Do() override;
    void Undo() override;
    [[nodiscard]] std::string_view Label() const override { return "Create"; }

private:
    EditorScene* m_scene;
    EntitySnapshot m_snapshot;
};

/// Destroys an entity; undo restores it under the same id.
class DestroyEntityCommand final : public ICommand
{
public:
    DestroyEntityCommand(EditorScene& scene, EditorEntityId id);

    void Do() override;
    void Undo() override;
    [[nodiscard]] std::string_view Label() const override { return "Delete"; }

private:
    EditorScene* m_scene;
    EntitySnapshot m_snapshot;
};

} // namespace interi::editor
