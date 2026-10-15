// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/FractureMesh.h>

#include <array>
#include <cstdint>
#include <vector>

namespace interi::destruction::testing
{

/// L-shaped prism: a 2 x 2 square minus its 1 x 1 upper-right quadrant, z in [0, 1]. Volume 3.
inline FractureMesh MakeLShape()
{
    static constexpr std::array<std::array<float, 2>, 6> kOutline{{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}};
    return MakePrismMesh(kOutline, 0.0f, 1.0f);
}

/// U-shaped prism opening towards +y: 3 x 2 with a 1 x 1 notch at x in [1, 2], y in [1, 2]; z in [0, 1]. Volume 5.
inline FractureMesh MakeUShape()
{
    static constexpr std::array<std::array<float, 2>, 8> kOutline{
        {{0, 0}, {3, 0}, {3, 2}, {2, 2}, {2, 1}, {1, 1}, {1, 2}, {0, 2}}};
    return MakePrismMesh(kOutline, 0.0f, 1.0f);
}

/// Square tube along z: outer square [0, 3]^2 with a [1, 2]^2 hole, z in [0, 1]. Volume 8.
inline FractureMesh MakeTube()
{
    FractureMesh mesh;
    const std::array<std::array<float, 2>, 4> outer{{{0, 0}, {3, 0}, {3, 3}, {0, 3}}};
    const std::array<std::array<float, 2>, 4> inner{{{1, 1}, {2, 1}, {2, 2}, {1, 2}}};
    // Positions: outer bottom 0-3, outer top 4-7, inner bottom 8-11, inner top 12-15.
    for (const auto* ring : {&outer, &inner})
    {
        for (const float z : {0.0f, 1.0f})
        {
            for (const auto& [x, y] : *ring)
            {
                mesh.positions.push_back({x, y, z});
            }
        }
    }
    const auto add = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c, const Vec3& normal)
    {
        FractureTriangle t;
        t.indices = {a, b, c};
        t.normals = {normal, normal, normal};
        mesh.triangles.push_back(t);
    };
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        const std::uint32_t j = (i + 1) % 4;
        const Vec3 outward =
            NormalizeOrZero(Vec3(outer[j][1] - outer[i][1], outer[i][0] - outer[j][0], 0.0f)); // right of the CCW edge
        add(i, j, 4 + j, outward);
        add(i, 4 + j, 4 + i, outward);
        // Inner walls face into the hole (towards its centre), i.e. the opposite winding.
        add(8 + j, 8 + i, 12 + i, outward * -1.0f);
        add(8 + j, 12 + i, 12 + j, outward * -1.0f);
        // Bottom (z = 0, facing -z) and top (z = 1, facing +z) rings: quad outer i, outer j, inner j, inner i.
        add(i, 8 + j, j, {0, 0, -1});
        add(i, 8 + i, 8 + j, {0, 0, -1});
        add(4 + i, 4 + j, 12 + j, {0, 0, 1});
        add(4 + i, 12 + j, 12 + i, {0, 0, 1});
    }
    return mesh;
}

inline CutPlane Plane(std::array<double, 3> point, std::array<double, 3> normal)
{
    return CutPlane::FromPointNormal(point, normal);
}

} // namespace interi::destruction::testing
