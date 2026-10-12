// SPDX-License-Identifier: MIT
#include <interi/core/math/Clipping.h>

#include <cstddef>
#include <cstdint>

namespace interi
{

namespace
{
enum class Side : std::uint8_t
{
    Front,
    Back,
    On
};

Side Classify(float distance, float epsilon) noexcept
{
    if (distance > epsilon)
    {
        return Side::Front;
    }
    return distance < -epsilon ? Side::Back : Side::On;
}

void DropIfDegenerate(std::vector<Vec3>& polygon)
{
    if (polygon.size() < 3)
    {
        polygon.clear();
    }
}
} // namespace

void SplitPolygon(std::span<const Vec3> polygon, const Plane& plane, std::vector<Vec3>& front, std::vector<Vec3>& back,
                  float epsilon)
{
    front.clear();
    back.clear();
    const std::size_t count = polygon.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec3& current = polygon[i];
        const Vec3& next = polygon[(i + 1) % count];
        const float currentDistance = plane.SignedDistance(current);
        const float nextDistance = plane.SignedDistance(next);
        const Side currentSide = Classify(currentDistance, epsilon);
        const Side nextSide = Classify(nextDistance, epsilon);

        if (currentSide != Side::Back)
        {
            front.push_back(current);
        }
        if (currentSide != Side::Front)
        {
            back.push_back(current);
        }
        // Strictly crossing edge: add the intersection point to both sides.
        if ((currentSide == Side::Front && nextSide == Side::Back) ||
            (currentSide == Side::Back && nextSide == Side::Front))
        {
            const float t = currentDistance / (currentDistance - nextDistance);
            const Vec3 intersection = Lerp(current, next, t);
            front.push_back(intersection);
            back.push_back(intersection);
        }
    }

    // A polygon lying entirely on one side (possibly touching the plane) belongs to that side only.
    bool anyFront = false;
    bool anyBack = false;
    for (const Vec3& p : polygon)
    {
        const Side side = Classify(plane.SignedDistance(p), epsilon);
        anyFront = anyFront || side == Side::Front;
        anyBack = anyBack || side == Side::Back;
    }
    if (!anyBack)
    {
        back.clear();
    }
    if (!anyFront)
    {
        front.clear();
    }
    if (!anyFront && !anyBack)
    {
        front.assign(polygon.begin(), polygon.end()); // coplanar: report it as front
    }
    DropIfDegenerate(front);
    DropIfDegenerate(back);
}

void ClipPolygon(std::span<const Vec3> polygon, const Plane& plane, std::vector<Vec3>& out, float epsilon)
{
    std::vector<Vec3> back;
    SplitPolygon(polygon, plane, out, back, epsilon);
}

} // namespace interi
