// SPDX-License-Identifier: MIT
#include <interi/render/Mesh.h>

namespace interi::render
{

namespace
{

/// Appends a quad with corners a, b, c, d (counter-clockwise seen from where `normal` points).
void AddQuad(MeshData& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal)
{
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (const Vec3& corner : {a, b, c, d})
    {
        mesh.vertices.push_back({corner, normal});
    }
    for (const std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u})
    {
        mesh.indices.push_back(base + index);
    }
}

} // namespace

MeshData MakeCube(float halfExtent)
{
    const float h = halfExtent;
    MeshData mesh;
    AddQuad(mesh, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});      // +X
    AddQuad(mesh, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0}); // -X
    AddQuad(mesh, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});      // +Y
    AddQuad(mesh, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0}); // -Y
    AddQuad(mesh, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});      // +Z
    AddQuad(mesh, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1}); // -Z
    return mesh;
}

MeshData MakePlane(float halfExtent)
{
    const float h = halfExtent;
    MeshData mesh;
    AddQuad(mesh, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {0, 1, 0});
    return mesh;
}

} // namespace interi::render
