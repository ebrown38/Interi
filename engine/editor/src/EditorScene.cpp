// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/EditorScene.h>

#include <cmath>

namespace interi::editor
{

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
    m_nextId = 1;
}

void EditorScene::Load(const gameplay::LevelDef& level)
{
    Clear();
    for (const gameplay::StaticBoxDef& box : level.staticBoxes)
    {
        (void)CreateStaticBox(box.transform, box.halfExtents);
    }
    for (const gameplay::DestructibleDef& destructible : level.destructibles)
    {
        (void)CreateDestructible(destructible.transform, destructible.asset);
    }
    for (const gameplay::SpawnPoint& spawn : level.spawnPoints)
    {
        Transform transform;
        transform.position = spawn.position;
        transform.rotation = YawRotation(spawn.yaw);
        (void)CreateSpawnPoint(transform, spawn.name);
    }
}

gameplay::LevelDef EditorScene::ToLevel() const
{
    gameplay::LevelDef level;
    for (const auto& [id, entity] : m_entities)
    {
        const Transform& transform = *m_world.Get<Transform>(entity);
        Transform placement;
        placement.position = transform.position;
        placement.rotation = transform.rotation;
        if (const auto* box = m_world.Get<StaticBoxComponent>(entity))
        {
            level.staticBoxes.push_back({.transform = placement, .halfExtents = box->halfExtents});
        }
        else if (const auto* destructible = m_world.Get<DestructiblePlacementComponent>(entity))
        {
            gameplay::DestructibleDef def;
            def.asset = destructible->asset;
            def.transform = placement;
            level.destructibles.push_back(std::move(def));
        }
        else if (const auto* spawn = m_world.Get<SpawnPointComponent>(entity))
        {
            level.spawnPoints.push_back(
                {.name = spawn->name, .position = transform.position, .yaw = YawOf(transform.rotation)});
        }
    }
    return level;
}

EditorEntityId EditorScene::Create(const Transform& transform)
{
    const EditorEntityId id = m_nextId++;
    m_entities.emplace(id, m_world.CreateEntity(EditorIdComponent{id}, transform));
    return id;
}

EditorEntityId EditorScene::CreateStaticBox(const Transform& transform, const Vec3& halfExtents)
{
    const EditorEntityId id = Create(transform);
    (void)m_world.Add(m_entities.at(id), StaticBoxComponent{halfExtents});
    return id;
}

EditorEntityId EditorScene::CreateDestructible(const Transform& transform, std::string asset)
{
    const EditorEntityId id = Create(transform);
    (void)m_world.Add(m_entities.at(id), DestructiblePlacementComponent{std::move(asset)});
    return id;
}

EditorEntityId EditorScene::CreateSpawnPoint(const Transform& transform, std::string name)
{
    const EditorEntityId id = Create(transform);
    (void)m_world.Add(m_entities.at(id), SpawnPointComponent{std::move(name)});
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

EntityKind EditorScene::KindOf(EditorEntityId id) const
{
    const auto entity = Find(id);
    if (!entity)
    {
        return EntityKind::None;
    }
    if (m_world.Has<StaticBoxComponent>(*entity))
    {
        return EntityKind::StaticBox;
    }
    if (m_world.Has<DestructiblePlacementComponent>(*entity))
    {
        return EntityKind::Destructible;
    }
    if (m_world.Has<SpawnPointComponent>(*entity))
    {
        return EntityKind::SpawnPoint;
    }
    return EntityKind::None;
}

std::optional<ComponentSnapshot> EditorScene::ReadComponent(EditorEntityId id, std::string_view component) const
{
    const auto entity = Find(id);
    const TypeReflection* reflection = m_reflection->Find(component);
    if (!entity || reflection == nullptr)
    {
        return std::nullopt;
    }
    // GetErased has no const overload; reading only stamps a change tick, which the editor never looks at.
    const void* data = const_cast<World&>(m_world).GetErased(*entity, reflection->type);
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
    return true;
}

std::optional<EntitySnapshot> EditorScene::Snapshot(EditorEntityId id) const
{
    const auto entity = Find(id);
    if (!entity)
    {
        return std::nullopt;
    }
    EntitySnapshot snapshot{.id = id, .components = {}};
    const interi::detail::Archetype& archetype = *m_world.Archetypes()[m_world.ArchetypeOf(*entity)];
    for (const interi::detail::Column& column : archetype.columns) // by type name
    {
        const TypeReflection* reflection = m_reflection->Find(column.type);
        if (auto component = reflection == nullptr ? std::nullopt : ReadComponent(id, reflection->name))
        {
            snapshot.components.push_back(std::move(*component));
        }
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
    const Entity entity = m_world.CreateEntity(EditorIdComponent{snapshot.id});
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
    m_nextId = std::max(m_nextId, snapshot.id + 1);
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
