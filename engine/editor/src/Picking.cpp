// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>

namespace interi::editor
{

AABB ChunkBounds(const destruction::DestructibleAsset& asset, std::uint32_t chunk)
{
    AABB bounds = AABB::Empty();
    if (chunk < asset.chunks.size())
    {
        for (const destruction::RenderVertex& vertex : asset.chunks[chunk].render.vertices)
        {
            bounds.Expand(vertex.position);
        }
    }
    return bounds;
}

std::optional<PickBox> PickBoxOf(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds)
{
    const auto entity = scene.Find(id);
    if (!entity)
    {
        return std::nullopt;
    }
    const World& world = scene.GetWorld();
    const Transform& transform = *world.Get<Transform>(*entity);
    if (const auto* box = world.Get<StaticBoxComponent>(*entity))
    {
        return PickBox{.centre = transform.position, .rotation = transform.rotation, .halfExtents = box->halfExtents};
    }
    if (const auto* placement = world.Get<DestructiblePlacementComponent>(*entity))
    {
        const std::optional<AABB> local = bounds ? bounds(placement->asset) : std::nullopt;
        if (!local || !local->IsValid())
        {
            return PickBox{.centre = transform.position + Vec3(0.0f, kPlaceholderHalfExtents.y, 0.0f),
                           .rotation = transform.rotation,
                           .halfExtents = kPlaceholderHalfExtents};
        }
        const Vec3 centre = (local->min + local->max) * 0.5f;
        return PickBox{.centre = transform.position + Rotate(transform.rotation, centre),
                       .rotation = transform.rotation,
                       .halfExtents = (local->max - local->min) * 0.5f};
    }
    if (world.Has<SpawnPointComponent>(*entity))
    {
        return PickBox{.centre = transform.position + Vec3(0.0f, kSpawnMarkerHalfExtents.y, 0.0f),
                       .rotation = transform.rotation,
                       .halfExtents = kSpawnMarkerHalfExtents};
    }
    return std::nullopt;
}

std::optional<PickHit> PickEntity(const EditorScene& scene, const Ray& ray, const AssetBoundsFn& bounds)
{
    std::optional<PickHit> best;
    for (const EditorEntityId id : scene.Ids())
    {
        const auto box = PickBoxOf(scene, id, bounds);
        if (!box)
        {
            continue;
        }
        const auto distance = RayBox(ray, box->centre, box->rotation, box->halfExtents);
        if (distance && (!best || *distance < best->distance))
        {
            best = PickHit{.id = id, .distance = *distance};
        }
    }
    return best;
}

} // namespace interi::editor
