// SPDX-License-Identifier: MIT
#pragma once

// Procedural levels for navigation tests: flat floors built from quads (counter-clockwise seen from above).

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <vector>

namespace interi::nav::testing
{

struct Geometry
{
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;

    /// A horizontal quad at height `y` covering [x0, x1] x [z0, z1].
    void AddFloor(float x0, float z0, float x1, float z1, float y = 0.0f)
    {
        const auto base = static_cast<std::uint32_t>(vertices.size());
        vertices.insert(vertices.end(), {Vec3(x0, y, z0), Vec3(x0, y, z1), Vec3(x1, y, z1), Vec3(x1, y, z0)});
        indices.insert(indices.end(), {base, base + 1, base + 3, base + 1, base + 2, base + 3});
    }
};

/// One 20 x 20 floor centred on the origin.
inline Geometry SimpleFloor()
{
    Geometry g;
    g.AddFloor(-10.0f, -10.0f, 10.0f, 10.0f);
    return g;
}

/// A 3 x 3 grid of 6 m tiles without the middle one: a square pit in the centre.
inline Geometry FloorWithPit()
{
    Geometry g;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            if (i == 1 && j == 1)
            {
                continue;
            }
            const float x = -9.0f + 6.0f * static_cast<float>(i);
            const float z = -9.0f + 6.0f * static_cast<float>(j);
            g.AddFloor(x, z, x + 6.0f, z + 6.0f);
        }
    }
    return g;
}

/// Two floors with a gap nobody can cross.
inline Geometry TwoIslands()
{
    Geometry g;
    g.AddFloor(-10.0f, -5.0f, -2.0f, 5.0f);
    g.AddFloor(2.0f, -5.0f, 10.0f, 5.0f);
    return g;
}

} // namespace interi::nav::testing
