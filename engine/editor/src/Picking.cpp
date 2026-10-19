// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>

#include <cmath>

namespace interi::editor
{

std::optional<AABB> LocalBounds(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds)
{
    const auto entity = scene.Find(id);
    if (!entity)
    {
        return std::nullopt;
    }
    const World& world = scene.GetWorld();
    const auto box = [](const Vec3& centre, const Vec3& half)
    { return AABB{.min = centre - half, .max = centre + half}; };
    if (const auto* primitive = world.Get<scene::PrimitiveComponent>(*entity))
    {
        const auto params = scene::ParamsOf(*primitive);
        if (!params)
        {
            return std::nullopt;
        }
        // A plane has no thickness; give it a sliver so rays can hit it.
        return box(primitive->offset, Max(scene::PrimitiveHalfExtents(*params), Vec3(0.005f)));
    }
    if (const auto* mesh = world.Get<scene::MeshComponent>(*entity))
    {
        const std::optional<AABB> local = bounds ? bounds(mesh->asset.path) : std::nullopt;
        return local && local->IsValid() ? AABB{.min = local->min + mesh->offset, .max = local->max + mesh->offset}
                                         : box(mesh->offset, kPlaceholderHalfExtents);
    }
    return std::nullopt;
}

std::optional<PickBox> PickBoxOf(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds)
{
    const std::optional<AABB> local = LocalBounds(scene, id, bounds);
    if (!local)
    {
        return std::nullopt;
    }
    const Transform transform = scene.WorldTransform(id);
    const Vec3 scale{std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z)};
    return PickBox{.centre = transform.TransformPoint((local->min + local->max) * 0.5f),
                   .rotation = transform.rotation,
                   .halfExtents = (local->max - local->min) * 0.5f * scale};
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
