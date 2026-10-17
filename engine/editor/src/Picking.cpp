// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/editor/Gizmo.h>
#include <interi/editor/Picking.h>

#include <cmath>

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

std::optional<float> RayMesh(const Ray& ray, const destruction::FractureMesh& mesh) noexcept
{
    // Moller-Trumbore per triangle.
    std::optional<float> best;
    for (const destruction::FractureTriangle& triangle : mesh.triangles)
    {
        const Vec3& a = mesh.positions[triangle.indices[0]];
        const Vec3 ab = mesh.positions[triangle.indices[1]] - a;
        const Vec3 ac = mesh.positions[triangle.indices[2]] - a;
        const Vec3 p = Cross(ray.direction, ac);
        const float determinant = Dot(ab, p);
        if (std::abs(determinant) < 1e-12f)
        {
            continue;
        }
        const float inverse = 1.0f / determinant;
        const Vec3 s = ray.origin - a;
        const float u = Dot(s, p) * inverse;
        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }
        const Vec3 q = Cross(s, ab);
        const float v = Dot(ray.direction, q) * inverse;
        if (v < 0.0f || u + v > 1.0f)
        {
            continue;
        }
        const float t = Dot(ac, q) * inverse;
        if (t >= 0.0f && (!best || t < *best))
        {
            best = t;
        }
    }
    return best;
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
