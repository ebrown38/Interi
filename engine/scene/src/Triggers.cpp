// SPDX-License-Identifier: MIT
#include <interi/ecs/Query.h>
#include <interi/scene/SceneComponents.h>
#include <interi/scene/Triggers.h>

#include <algorithm>
#include <numeric>

namespace interi::scene
{

void RegisterTriggerReflection(values::ReflectionRegistry& registry)
{
    values::ValueMeta length;
    length.min = 0.0;
    length.unit = "m";
    values::ValueMeta layers;
    layers.tooltip = "Bit mask of actor layers.";
    registry.Register<TriggerVolumeComponent>({
        values::EnumField("shape", &TriggerVolumeComponent::shape, {"Box", "Sphere"}),
        values::Field("halfExtents", &TriggerVolumeComponent::halfExtents, length),
        values::Field("radius", &TriggerVolumeComponent::radius, length),
        values::Field("tag", &TriggerVolumeComponent::tag),
        values::Field("mask", &TriggerVolumeComponent::mask, layers),
    });
    registry.Register<TriggerActorComponent>({
        values::Field("radius", &TriggerActorComponent::radius, length),
        values::Field("layers", &TriggerActorComponent::layers, layers),
    });
}

TriggerScan ScanTriggers(World& world)
{
    TriggerScan scan;
    Query().With<StableIdComponent, Transform>().ForEachChunk(
        world,
        [&](ChunkView& view)
        {
            const auto ids = view.Read<StableIdComponent>();
            const auto entities = view.Entities();
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                const auto* transform = world.Get<Transform>(entities[i]);
                if (const auto* volume = world.Get<TriggerVolumeComponent>(entities[i]))
                {
                    const float largest = std::max({transform->scale.x, transform->scale.y, transform->scale.z});
                    scan.volumes.push_back(
                        {.id = ids[i].id,
                         .shape = volume->shape.name == "Sphere" ? VolumeShape::Sphere : VolumeShape::Box,
                         .center = transform->position,
                         .halfExtents = volume->halfExtents * transform->scale,
                         .radius = volume->radius * largest,
                         .mask = volume->mask});
                    scan.tags.push_back(volume->tag);
                }
                if (const auto* actor = world.Get<TriggerActorComponent>(entities[i]))
                {
                    scan.actors.push_back({.id = ids[i].id,
                                           .position = transform->position,
                                           .radius = actor->radius,
                                           .layers = actor->layers});
                }
            }
        });
    // Chunk order changes as entities are removed (swap-remove): put everything in id order.
    std::vector<std::size_t> order(scan.volumes.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::sort(order, {}, [&](std::size_t i) { return scan.volumes[i].id; });
    TriggerScan sorted;
    for (const std::size_t i : order)
    {
        sorted.volumes.push_back(scan.volumes[i]);
        sorted.tags.push_back(std::move(scan.tags[i]));
    }
    sorted.actors = std::move(scan.actors);
    std::ranges::sort(sorted.actors, {}, &TriggerActor::id);
    return sorted;
}

} // namespace interi::scene
