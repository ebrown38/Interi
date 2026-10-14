// SPDX-License-Identifier: MIT
#include <interi/physics/JoltMath.h>
#include <interi/physics/Queries.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
// clang-format on

#include <algorithm>

namespace interi::physics
{

namespace
{

class LayerMaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit LayerMaskFilter(LayerMask mask) noexcept
        : m_mask(mask)
    {
    }

    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return (m_mask & (LayerMask{1} << layer)) != 0;
    }

private:
    LayerMask m_mask;
};

/// Skips broadphase trees that hold none of the masked layers.
class BroadPhaseMaskFilter final : public JPH::BroadPhaseLayerFilter
{
public:
    explicit BroadPhaseMaskFilter(LayerMask mask) noexcept
        : m_mask(mask)
    {
    }

    [[nodiscard]] bool ShouldCollide(JPH::BroadPhaseLayer broadPhase) const override
    {
        for (std::uint32_t layer = 0; layer < kObjectLayerCount; ++layer)
        {
            if ((m_mask & (LayerMask{1} << layer)) != 0 &&
                BroadPhaseLayerOf(static_cast<ObjectLayer>(layer)) == broadPhase)
            {
                return true;
            }
        }
        return false;
    }

private:
    LayerMask m_mask;
};

/// Fills the body-dependent fields of a hit: entity and surface normal.
QueryHit MakeHit(const PhysicsWorld& world, const JPH::BodyID& body, const JPH::SubShapeID& subShape,
                 JPH::RVec3Arg point, float fraction)
{
    QueryHit hit{.body = body,
                 .entity = world.EntityOf(body),
                 .subShape = subShape.GetValue(),
                 .point = FromJolt(point),
                 .normal = {},
                 .fraction = fraction};
    const JPH::BodyLockRead lock(world.System().GetBodyLockInterface(), body);
    if (lock.Succeeded())
    {
        hit.normal = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
    }
    return hit;
}

} // namespace

std::optional<QueryHit> Raycast(const PhysicsWorld& world, const Vec3& origin, const Vec3& displacement,
                                LayerMask layers)
{
    const JPH::RRayCast ray(ToJolt(origin), ToJolt(displacement));
    JPH::RayCastResult result;
    if (!world.System().GetNarrowPhaseQuery().CastRay(ray, result, BroadPhaseMaskFilter(layers),
                                                      LayerMaskFilter(layers)))
    {
        return std::nullopt;
    }
    return MakeHit(world, result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), result.mFraction);
}

std::optional<QueryHit> SphereCast(const PhysicsWorld& world, const Vec3& start, float radius, const Vec3& displacement,
                                   LayerMask layers)
{
    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    const JPH::RShapeCast cast(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(start)),
                               ToJolt(displacement));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true; // overlaps at the start report their deepest point
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    world.System().GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector,
                                                   BroadPhaseMaskFilter(layers), LayerMaskFilter(layers));
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const JPH::ShapeCastResult& result = collector.mHit;
    return MakeHit(world, result.mBodyID2, result.mSubShapeID2, result.mContactPointOn2, result.mFraction);
}

void OverlapSphere(const PhysicsWorld& world, const Vec3& center, float radius, std::vector<QueryHit>& out,
                   LayerMask layers)
{
    out.clear();
    const JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    world.System().GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJolt(center)), settings, JPH::RVec3::sZero(), collector,
        BroadPhaseMaskFilter(layers), LayerMaskFilter(layers));
    // One hit per body (a compound reports one per touched part): keep the deepest.
    std::ranges::sort(collector.mHits,
                      [](const JPH::CollideShapeResult& a, const JPH::CollideShapeResult& b)
                      {
                          if (a.mBodyID2 != b.mBodyID2)
                          {
                              return a.mBodyID2 < b.mBodyID2;
                          }
                          return a.mPenetrationDepth > b.mPenetrationDepth;
                      });
    for (std::size_t i = 0; i < collector.mHits.size(); ++i)
    {
        const JPH::CollideShapeResult& result = collector.mHits[i];
        if (i > 0 && collector.mHits[i - 1].mBodyID2 == result.mBodyID2)
        {
            continue;
        }
        out.push_back(MakeHit(world, result.mBodyID2, result.mSubShapeID2, result.mContactPointOn2, 0.0f));
    }
}

} // namespace interi::physics
