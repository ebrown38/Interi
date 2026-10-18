// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/EditorScene.h>

#include <algorithm>
#include <cmath>

namespace interi::editor
{

namespace
{

[[nodiscard]] bool IsStandard(std::string_view component) noexcept
{
    return component == scene::NameComponent::kComponentName || component == scene::ParentComponent::kComponentName ||
           component == scene::LocalTransformComponent::kComponentName;
}

} // namespace

EditorScene::EditorScene(const ReflectionRegistry& reflection)
    : m_reflection(&reflection)
{
}

void EditorScene::Clear()
{
    for (const auto& [id, entity] : m_entities)
    {
        (void)m_world.DestroyEntity(entity);
    }
    m_entities.clear();
    m_kept.clear();
    m_hierarchy = {};
    m_nextId = 1;
}

Result<std::vector<std::string>> EditorScene::Load(const scene::SceneDocument& document)
{
    World scratch;
    INTERI_TRY(scene::InstantiateScene(document, scratch, *m_reflection)); // validate before touching the scene
    Clear();
    INTERI_TRY_ASSIGN(loaded, scene::InstantiateScene(document, m_world, *m_reflection));
    m_entities = std::move(loaded.entities);
    m_kept = std::move(loaded.kept);
    m_nextId = m_entities.empty() ? 1 : m_entities.rbegin()->first + 1;
    Refresh();
    return std::move(loaded.warnings);
}

scene::SceneDocument EditorScene::ToDocument() const
{
    return scene::CaptureScene(m_world, *m_reflection, &m_kept);
}

void EditorScene::Refresh()
{
    m_hierarchy = scene::PropagateTransforms(m_world);
}

EditorEntityId EditorScene::CreateObject(std::string name, const Transform& local, EditorEntityId parent)
{
    const EditorEntityId id = m_nextId++;
    m_entities.emplace(id, scene::CreateObject(m_world, id, std::move(name), parent, local));
    Refresh();
    return id;
}

EditorEntityId EditorScene::CreatePrimitive(const scene::PrimitiveParams& params, const Transform& local,
                                            std::string name, EditorEntityId parent)
{
    if (name.empty())
    {
        name = std::string(scene::ToString(params.shape));
    }
    const EditorEntityId id = CreateObject(std::move(name), local, parent);
    (void)m_world.Add(m_entities.at(id), scene::MakePrimitiveComponent(params));
    return id;
}

bool EditorScene::Destroy(EditorEntityId id)
{
    const auto found = m_entities.find(id);
    if (found == m_entities.end())
    {
        return false;
    }
    (void)m_world.DestroyEntity(found->second);
    m_entities.erase(found);
    m_kept.erase(id);
    Refresh();
    return true;
}

std::optional<Entity> EditorScene::Find(EditorEntityId id) const
{
    const auto found = m_entities.find(id);
    if (found == m_entities.end())
    {
        return std::nullopt;
    }
    return found->second;
}

std::vector<EditorEntityId> EditorScene::Ids() const
{
    std::vector<EditorEntityId> ids;
    ids.reserve(m_entities.size());
    for (const auto& [id, entity] : m_entities)
    {
        ids.push_back(id);
    }
    return ids;
}

std::string_view EditorScene::NameOf(EditorEntityId id) const
{
    const auto entity = Find(id);
    const auto* name = entity ? m_world.Get<scene::NameComponent>(*entity) : nullptr;
    return name == nullptr ? std::string_view{} : std::string_view(name->name);
}

EditorEntityId EditorScene::ParentOf(EditorEntityId id) const
{
    const auto found = m_hierarchy.nodes.find(id);
    return found == m_hierarchy.nodes.end() ? 0 : found->second.parent;
}

Transform EditorScene::WorldTransform(EditorEntityId id) const
{
    const auto entity = Find(id);
    const auto* transform = entity ? m_world.Get<Transform>(*entity) : nullptr;
    return transform == nullptr ? Transform{} : *transform;
}

Transform EditorScene::LocalTransform(EditorEntityId id) const
{
    const auto entity = Find(id);
    const auto* local = entity ? m_world.Get<scene::LocalTransformComponent>(*entity) : nullptr;
    return local == nullptr ? Transform{} : local->ToTransform();
}

std::optional<ComponentSnapshot> EditorScene::ReadComponent(EditorEntityId id, std::string_view component) const
{
    const auto entity = Find(id);
    const TypeReflection* reflection = m_reflection->Find(component);
    if (!entity || reflection == nullptr)
    {
        return std::nullopt;
    }
    const void* data = m_world.GetErased(*entity, reflection->type);
    if (data == nullptr)
    {
        return std::nullopt;
    }
    ComponentSnapshot snapshot{.component = reflection->name, .values = {}};
    snapshot.values.reserve(reflection->fields.size());
    for (const FieldInfo& field : reflection->fields)
    {
        snapshot.values.push_back(ReadField(field, data));
    }
    return snapshot;
}

bool EditorScene::WriteComponent(EditorEntityId id, const ComponentSnapshot& snapshot)
{
    const auto entity = Find(id);
    const TypeReflection* reflection = m_reflection->Find(snapshot.component);
    if (!entity || reflection == nullptr || snapshot.values.size() != reflection->fields.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < snapshot.values.size(); ++i)
    {
        if (TypeOf(snapshot.values[i]) != reflection->fields[i].type)
        {
            return false;
        }
    }
    void* data = m_world.GetErased(*entity, reflection->type);
    if (data == nullptr)
    {
        return false;
    }
    for (std::size_t i = 0; i < snapshot.values.size(); ++i)
    {
        (void)WriteField(reflection->fields[i], data, snapshot.values[i]);
    }
    if (snapshot.component == scene::ParentComponent::kComponentName ||
        snapshot.component == scene::LocalTransformComponent::kComponentName)
    {
        Refresh();
    }
    return true;
}

bool EditorScene::AddComponent(EditorEntityId id, std::string_view component)
{
    const auto entity = Find(id);
    const TypeReflection* reflection = m_reflection->Find(component);
    if (!entity || reflection == nullptr || m_world.GetErased(*entity, reflection->type) != nullptr)
    {
        return false;
    }
    reflection->addDefault(m_world, *entity);
    return true;
}

bool EditorScene::RemoveComponent(EditorEntityId id, std::string_view component)
{
    const auto entity = Find(id);
    const TypeReflection* reflection = m_reflection->Find(component);
    if (!entity || reflection == nullptr || IsStandard(reflection->name))
    {
        return false;
    }
    return m_world.RemoveErased(*entity, reflection->type);
}

std::optional<EntitySnapshot> EditorScene::Snapshot(EditorEntityId id) const
{
    const auto entity = Find(id);
    if (!entity)
    {
        return std::nullopt;
    }
    EntitySnapshot snapshot{.id = id, .components = {}, .kept = {}};
    const interi::detail::Archetype& archetype = *m_world.Archetypes()[m_world.ArchetypeOf(*entity)];
    for (const interi::detail::Column& column : archetype.columns) // by type name
    {
        const TypeReflection* reflection = m_reflection->Find(column.type);
        if (auto component = reflection == nullptr ? std::nullopt : ReadComponent(id, reflection->name))
        {
            snapshot.components.push_back(std::move(*component));
        }
    }
    if (const auto kept = m_kept.find(id); kept != m_kept.end())
    {
        snapshot.kept = kept->second;
    }
    return snapshot;
}

bool EditorScene::Restore(const EntitySnapshot& snapshot)
{
    if (snapshot.id == 0 || m_entities.contains(snapshot.id))
    {
        return false;
    }
    for (const ComponentSnapshot& component : snapshot.components)
    {
        if (m_reflection->Find(component.component) == nullptr)
        {
            return false;
        }
    }
    const Entity entity = scene::CreateObject(m_world, snapshot.id, {});
    m_entities.emplace(snapshot.id, entity);
    for (const ComponentSnapshot& component : snapshot.components)
    {
        const TypeReflection& reflection = *m_reflection->Find(component.component);
        if (m_world.GetErased(entity, reflection.type) == nullptr)
        {
            reflection.addDefault(m_world, entity);
        }
        (void)WriteComponent(snapshot.id, component);
    }
    if (!snapshot.kept.empty())
    {
        m_kept[snapshot.id] = snapshot.kept;
    }
    m_nextId = std::max(m_nextId, snapshot.id + 1);
    Refresh();
    return true;
}

Quat YawRotation(float yaw) noexcept
{
    return Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
}

float YawOf(const Quat& rotation) noexcept
{
    const Vec3 forward = Rotate(rotation, {0.0f, 0.0f, -1.0f});
    return std::atan2(-forward.x, -forward.z);
}

} // namespace interi::editor
