// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <vector>

namespace interi::render
{

/// Vertex layout shaders pull from the MeshPool vertex buffer (see engine/render/shaders/Forward.vs.hlsl).
struct Vertex
{
    Vec3 position;
    Vec3 normal;
};
static_assert(sizeof(Vertex) == 24, "shaders read vertices with a 24-byte stride");

/// CPU mesh: triangles are counter-clockwise when seen from outside.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// Axis-aligned cube centred on the origin, with flat per-face normals.
[[nodiscard]] MeshData MakeCube(float halfExtent = 0.5f);
/// Square in the XZ plane facing +Y.
[[nodiscard]] MeshData MakePlane(float halfExtent);

} // namespace interi::render
