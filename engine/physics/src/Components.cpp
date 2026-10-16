// SPDX-License-Identifier: MIT
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
// clang-format on

#include <algorithm>
#include <string>
#include <utility>

namespace interi::physics
{

namespace
{

Result<ColliderComponent> FromShapeResult(const JPH::ShapeSettings::ShapeResult& result, const char* what)
{
    if (result.HasError())
    {
        const JPH::String& error = result.GetError();
        std::string message = std::string(what) + ": ";
        message.append(error.data(), error.size()); // JPH::String may use Jolt's allocator
        return MakeError(ErrorCode::Degenerate, std::move(message));
    }
    return ColliderComponent{.shape = result.Get()};
}

} // namespace

ColliderComponent ColliderComponent::Box(const Vec3& halfExtents)
{
    // Jolt's convex radius must not exceed the half extents.
    const float radius = std::min({JPH::cDefaultConvexRadius, halfExtents.x, halfExtents.y, halfExtents.z});
    return {.shape = new JPH::BoxShape(ToJolt(halfExtents), radius)};
}

ColliderComponent ColliderComponent::Sphere(float radius)
{
    return {.shape = new JPH::SphereShape(radius)};
}

ColliderComponent ColliderComponent::Cylinder(float halfHeight, float radius)
{
    const float convexRadius = std::min({JPH::cDefaultConvexRadius, halfHeight, radius});
    return {.shape = new JPH::CylinderShape(halfHeight, radius, convexRadius)};
}

Result<ColliderComponent> ColliderComponent::ConvexHull(std::span<const Vec3> points)
{
    JPH::Array<JPH::Vec3> joltPoints;
    joltPoints.reserve(points.size());
    for (const Vec3& point : points)
    {
        joltPoints.push_back(ToJolt(point));
    }
    const JPH::ConvexHullShapeSettings settings(joltPoints);
    return FromShapeResult(settings.Create(), "convex hull");
}

Result<ColliderComponent> ColliderComponent::Compound(std::span<const ColliderPart> parts)
{
    if (parts.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "compound collider needs at least one part");
    }
    JPH::StaticCompoundShapeSettings settings;
    for (const ColliderPart& part : parts)
    {
        settings.AddShape(ToJolt(part.position), ToJolt(part.rotation), part.collider.shape.GetPtr());
    }
    return FromShapeResult(settings.Create(), "compound");
}

} // namespace interi::physics
