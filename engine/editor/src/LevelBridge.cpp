// SPDX-License-Identifier: MIT
#include <interi/editor/LevelBridge.h>

#include <format>

namespace interi::editor
{

void RegisterLevelReflection(ReflectionRegistry& registry)
{
    registry.Register<DestructiblePlacementComponent>({Field("asset", &DestructiblePlacementComponent::asset)});
    registry.Register<SpawnPointComponent>({Field("name", &SpawnPointComponent::name)});
}

EditorEntityId CreateDestructible(EditorScene& scene, const Transform& transform, std::string asset)
{
    const EditorEntityId id = scene.CreateObject("Destructible", transform);
    if (const auto entity = scene.Find(id))
    {
        (void)scene.GetWorld().Add(*entity, DestructiblePlacementComponent{std::move(asset)});
    }
    return id;
}

EditorEntityId CreateSpawnPoint(EditorScene& scene, const Transform& transform, std::string name)
{
    const EditorEntityId id = scene.CreateObject(std::format("Spawn {}", name), transform);
    if (const auto entity = scene.Find(id))
    {
        (void)scene.GetWorld().Add(*entity, SpawnPointComponent{std::move(name)});
    }
    return id;
}

void ImportLevel(const gameplay::LevelDef& level, EditorScene& scene)
{
    for (const gameplay::StaticBoxDef& box : level.staticBoxes)
    {
        scene::PrimitiveParams params;
        params.shape = scene::PrimitiveShape::Box;
        params.size = box.halfExtents * 2.0f;
        Transform placement;
        placement.position = box.transform.position;
        placement.rotation = box.transform.rotation;
        (void)scene.CreatePrimitive(params, placement, "Static box");
    }
    for (const gameplay::DestructibleDef& destructible : level.destructibles)
    {
        Transform placement;
        placement.position = destructible.transform.position;
        placement.rotation = destructible.transform.rotation;
        (void)CreateDestructible(scene, placement, destructible.asset);
    }
    for (const gameplay::SpawnPoint& spawn : level.spawnPoints)
    {
        Transform transform;
        transform.position = spawn.position;
        transform.rotation = YawRotation(spawn.yaw);
        (void)CreateSpawnPoint(scene, transform, spawn.name);
    }
}

gameplay::LevelDef ExportLevel(const EditorScene& scene, std::vector<std::string>* warnings)
{
    gameplay::LevelDef level;
    const World& world = scene.GetWorld();
    for (const EditorEntityId id : scene.Ids())
    {
        const std::optional<Entity> found = scene.Find(id);
        if (!found)
        {
            continue;
        }
        const Entity entity = *found;
        const Transform transform = scene.WorldTransform(id);
        Transform placement;
        placement.position = transform.position;
        placement.rotation = transform.rotation;
        if (const auto* primitive = world.Get<scene::PrimitiveComponent>(entity);
            primitive != nullptr && primitive->shape.name == scene::ToString(scene::PrimitiveShape::Box))
        {
            level.staticBoxes.push_back(
                {.transform = placement, .halfExtents = primitive->size * transform.scale * 0.5f});
        }
        else if (const auto* destructible = world.Get<DestructiblePlacementComponent>(entity))
        {
            gameplay::DestructibleDef def;
            def.asset = destructible->asset;
            def.transform = placement;
            level.destructibles.push_back(std::move(def));
        }
        else if (const auto* spawn = world.Get<SpawnPointComponent>(entity))
        {
            level.spawnPoints.push_back(
                {.name = spawn->name, .position = transform.position, .yaw = YawOf(transform.rotation)});
        }
        else if (warnings != nullptr &&
                 (world.Has<scene::PrimitiveComponent>(entity) || world.Has<scene::MeshComponent>(entity)))
        {
            warnings->push_back(std::format("'{}' ({}): only Box primitives export to a level", scene.NameOf(id), id));
        }
    }
    return level;
}

} // namespace interi::editor
