// SPDX-License-Identifier: MIT
#include <interi/core/math/Vec.h>
#include <interi/render/Mesh.h>

#include <gtest/gtest.h>

#include <cmath>

using namespace interi;
using namespace interi::render;

namespace
{

/// Every triangle is counter-clockwise seen from outside: its geometric normal agrees with the vertex normals and
/// points away from the centre.
void ExpectOutwardCounterClockwise(const MeshData& mesh)
{
    ASSERT_EQ(mesh.indices.size() % 3, 0u);
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3)
    {
        const Vertex& a = mesh.vertices[mesh.indices[t]];
        const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[t + 2]];
        const Vec3 geometric = Cross(b.position - a.position, c.position - a.position);
        EXPECT_GT(Dot(geometric, a.normal), 0.0f) << "triangle " << t / 3;
        EXPECT_GE(Dot(a.position, a.normal), 0.0f) << "triangle " << t / 3;
    }
}

} // namespace

TEST(Mesh, Cube_HasSixFlatFaces)
{
    const MeshData cube = MakeCube(0.5f);
    EXPECT_EQ(cube.vertices.size(), 24u);
    EXPECT_EQ(cube.indices.size(), 36u);
    for (const Vertex& vertex : cube.vertices)
    {
        EXPECT_NEAR(std::abs(vertex.position.x), 0.5f, 1e-6f);
        EXPECT_NEAR(Length(vertex.normal), 1.0f, 1e-6f);
    }
}

TEST(Mesh, Cube_TrianglesAreCounterClockwiseFromOutside)
{
    ExpectOutwardCounterClockwise(MakeCube(1.0f));
}

TEST(Mesh, Plane_FacesUp)
{
    const MeshData plane = MakePlane(2.0f);
    ExpectOutwardCounterClockwise(plane);
    for (const Vertex& vertex : plane.vertices)
    {
        EXPECT_EQ(vertex.normal.y, 1.0f);
    }
}
