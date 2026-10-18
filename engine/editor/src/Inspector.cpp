// SPDX-License-Identifier: MIT
#include <interi/editor/Commands.h>
#include <interi/editor/Inspector.h>

#include <format>
#include <map>
#include <memory>

namespace interi::editor
{

namespace
{

[[nodiscard]] ComponentSnapshot LocalSnapshot(const Transform& local)
{
    return {.component = scene::LocalTransformComponent::kComponentName,
            .values = {local.position, local.rotation, local.scale}};
}

} // namespace

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

bool EditLocalTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& local,
                        std::uint64_t mergeKey)
{
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, scene::LocalTransformComponent::kComponentName);
    if (!before)
    {
        return false;
    }
    ComponentSnapshot after = LocalSnapshot(local);
    if (after.values == before->values)
    {
        return false;
    }
    undo.Execute(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), std::move(after), mergeKey));
    return true;
}

bool EditTransform(EditorScene& scene, UndoStack& undo, EditorEntityId id, const Transform& world,
                   std::uint64_t mergeKey)
{
    const EditorEntityId parent = scene.ParentOf(id);
    const Transform local = parent == 0 ? world : scene.WorldTransform(parent).Inverse() * world;
    return EditLocalTransform(scene, undo, id, local, mergeKey);
}

bool Reparent(EditorScene& scene, UndoStack& undo, EditorEntityId id, EditorEntityId parent)
{
    if (!scene.Contains(id) || (parent != 0 && !scene.Contains(parent)) || scene.ParentOf(id) == parent ||
        (parent != 0 && scene.Hierarchy().IsWithin(parent, id)))
    {
        return false;
    }
    std::optional<ComponentSnapshot> link = scene.ReadComponent(id, scene::ParentComponent::kComponentName);
    std::optional<ComponentSnapshot> before = scene.ReadComponent(id, scene::LocalTransformComponent::kComponentName);
    if (!link || !before)
    {
        return false;
    }
    const Transform world = scene.WorldTransform(id);
    const Transform local = parent == 0 ? world : scene.WorldTransform(parent).Inverse() * world;
    std::vector<std::unique_ptr<ICommand>> commands;
    commands.push_back(std::make_unique<SetComponentCommand>(
        scene, id, std::move(*link),
        ComponentSnapshot{.component = scene::ParentComponent::kComponentName, .values = {values::EntityRef{parent}}}));
    commands.push_back(std::make_unique<SetComponentCommand>(scene, id, std::move(*before), LocalSnapshot(local)));
    undo.Execute(std::make_unique<MacroCommand>("Reparent", std::move(commands)));
    return true;
}

bool AddComponent(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component)
{
    const TypeReflection* reflection = scene.Reflection().Find(component);
    if (reflection == nullptr || !scene.Contains(id) || scene.ReadComponent(id, component).has_value())
    {
        return false;
    }
    undo.Execute(std::make_unique<AddComponentCommand>(scene, id, reflection->name));
    return true;
}

bool RemoveComponent(EditorScene& scene, UndoStack& undo, EditorEntityId id, std::string_view component)
{
    std::optional<ComponentSnapshot> snapshot = scene.ReadComponent(id, component);
    if (!snapshot || component == scene::NameComponent::kComponentName ||
        component == scene::ParentComponent::kComponentName ||
        component == scene::LocalTransformComponent::kComponentName)
    {
        return false;
    }
    undo.Execute(std::make_unique<RemoveComponentCommand>(scene, id, std::move(*snapshot)));
    return true;
}

std::vector<OutlinerRow> OutlinerRows(const EditorScene& scene)
{
    std::vector<OutlinerRow> rows;
    const scene::HierarchyIndex& hierarchy = scene.Hierarchy();
    std::map<EditorEntityId, std::uint32_t> depths;
    for (const EditorEntityId id : hierarchy.PreOrder())
    {
        const scene::HierarchyIndex::Node& node = hierarchy.nodes.at(id);
        const std::uint32_t depth = node.parent == 0 ? 0 : depths.at(node.parent) + 1;
        depths.emplace(id, depth);
        const std::string_view name = scene.NameOf(id);
        rows.push_back({.id = id,
                        .depth = depth,
                        .hasChildren = !node.children.empty(),
                        .label = name.empty() ? std::format("Object {}", id) : std::string(name)});
    }
    return rows;
}

} // namespace interi::editor
