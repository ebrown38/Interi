// SPDX-License-Identifier: MIT
#include <interi/editor/Commands.h>
#include <interi/editor/Inspector.h>

#include <format>
#include <memory>

namespace interi::editor
{

bool EditField(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component,
               std::string_view field, const FieldValue& value, std::uint64_t mergeKey)
{
    const TypeReflection* reflection = scene.Reflection().Find(component);
    const FieldInfo* info = reflection == nullptr ? nullptr : reflection->Find(field);
    if (info == nullptr || info->type != TypeOf(value))
    {
        return false;
    }
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, component);
    if (!before)
    {
        return false;
    }
    const auto index = static_cast<std::size_t>(info - reflection->fields.data());
    if (before->values[index] == value)
    {
        return false;
    }
    ComponentSnapshot after = *before;
    after.values[index] = value;
    undo.Execute(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), std::move(after), mergeKey));
    return true;
}

bool EditTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& transform,
                   std::uint64_t mergeKey)
{
    const TypeReflection* reflection = scene.Reflection().Find(ComponentId<Transform>());
    std::optional<ComponentSnapshot> before =
        reflection == nullptr ? std::nullopt : scene.ReadComponent(id, reflection->name);
    if (!before)
    {
        return false;
    }
    ComponentSnapshot after{.component = before->component,
                            .values = {transform.position, transform.rotation, transform.scale}};
    if (after.values == before->values)
    {
        return false;
    }
    undo.Execute(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), std::move(after), mergeKey));
    return true;
}

std::vector<OutlinerRow> OutlinerRows(const EditorScene& scene)
{
    std::vector<OutlinerRow> rows;
    const World& world = scene.GetWorld();
    for (const EditorEntityId id : scene.Ids())
    {
        const auto entity = scene.Find(id);
        if (!entity)
        {
            continue;
        }
        OutlinerRow row{.id = id, .kind = scene.KindOf(id), .label = {}};
        switch (row.kind)
        {
        case EntityKind::StaticBox:
            row.label = std::format("Static box {}", id);
            break;
        case EntityKind::Destructible:
            row.label =
                std::format("Destructible {} ({})", id, world.Get<DestructiblePlacementComponent>(*entity)->asset);
            break;
        case EntityKind::SpawnPoint:
            row.label = std::format("Spawn {} ({})", id, world.Get<SpawnPointComponent>(*entity)->name);
            break;
        case EntityKind::None:
            row.label = std::format("Entity {}", id);
            break;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace interi::editor
