// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>

#include "DestructionTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::Vec3;

constexpr double kVolumeTolerance = 1e-5;

FractureMesh UnitCube()
{
    return MakeBoxMesh({0, 0, 0}, {1, 1, 1});
}

FractureMesh Cut(const FractureMesh& mesh, std::array<double, 3> point, std::array<double, 3> normal)
{
    return CutMesh(mesh, Plane(point, normal), CutTolerance(mesh));
}

} // namespace

TEST(FractureMesh, Box_WatertightWithUnitVolume)
{
    const FractureMesh cube = UnitCube();
    EXPECT_TRUE(IsWatertight(cube));
    EXPECT_NEAR(ComputeVolume(cube), 1.0, kVolumeTolerance);
    const Vec3 centroid = ComputeCentroid(cube);
    EXPECT_NEAR(centroid.x, 0.5f, 1e-6f);
    EXPECT_NEAR(centroid.y, 0.5f, 1e-6f);
    EXPECT_NEAR(centroid.z, 0.5f, 1e-6f);
}

TEST(FractureMesh, Prisms_WatertightWithExpectedVolume)
{
    EXPECT_TRUE(IsWatertight(MakeLShape()));
    EXPECT_NEAR(ComputeVolume(MakeLShape()), 3.0, kVolumeTolerance);
    EXPECT_TRUE(IsWatertight(MakeUShape()));
    EXPECT_NEAR(ComputeVolume(MakeUShape()), 5.0, kVolumeTolerance);
    EXPECT_TRUE(IsWatertight(MakeTube()));
    EXPECT_NEAR(ComputeVolume(MakeTube()), 8.0, kVolumeTolerance);
}

TEST(FractureMesh, MissingTriangle_NotWatertight)
{
    FractureMesh cube = UnitCube();
    cube.triangles.pop_back();
    EXPECT_FALSE(IsWatertight(cube));
}

TEST(FractureMesh, ContainsPoint_InsideAndOutsideLShape)
{
    const FractureMesh l = MakeLShape();
    EXPECT_TRUE(ContainsPoint(l, {0.5f, 0.5f, 0.5f}));
    EXPECT_TRUE(ContainsPoint(l, {1.5f, 0.5f, 0.5f}));
    EXPECT_FALSE(ContainsPoint(l, {1.5f, 1.5f, 0.5f})); // the notch
    EXPECT_FALSE(ContainsPoint(l, {0.5f, 0.5f, 1.5f}));
}

TEST(FractureMesh, WeldIdenticalPositions_RejoinsUnweldedBox)
{
    const FractureMesh cube = UnitCube();
    FractureMesh unwelded;
    for (FractureTriangle t : cube.triangles)
    {
        for (std::uint32_t& index : t.indices)
        {
            unwelded.positions.push_back(cube.positions[index]);
            index = static_cast<std::uint32_t>(unwelded.positions.size() - 1);
        }
        unwelded.triangles.push_back(t);
    }
    EXPECT_FALSE(IsWatertight(unwelded));
    const FractureMesh welded = WeldIdenticalPositions(unwelded);
    EXPECT_EQ(welded.positions.size(), 8u);
    EXPECT_TRUE(IsWatertight(welded));
}

TEST(MeshCut, CubeHalf_HalfVolumeWatertightWithCap)
{
    const FractureMesh half = Cut(UnitCube(), {0.5, 0, 0}, {1, 0, 0});
    EXPECT_TRUE(IsWatertight(half));
    EXPECT_NEAR(ComputeVolume(half), 0.5, kVolumeTolerance);
    EXPECT_NEAR(ComputeInteriorArea(half), 1.0, 1e-5);
    for (const Vec3& p : half.positions)
    {
        EXPECT_LE(p.x, 0.5f);
    }
    for (const FractureTriangle& t : half.triangles)
    {
        if (t.interior)
        {
            EXPECT_EQ(t.normals[0], Vec3(1, 0, 0));
        }
    }
}

TEST(MeshCut, PlaneMisses_KeepsAllOrNothing)
{
    const FractureMesh cube = UnitCube();
    const FractureMesh all = Cut(cube, {2, 0, 0}, {1, 0, 0});
    EXPECT_EQ(all.triangles.size(), cube.triangles.size());
    EXPECT_NEAR(ComputeVolume(all), 1.0, kVolumeTolerance);
    EXPECT_TRUE(Cut(cube, {-1, 0, 0}, {1, 0, 0}).triangles.empty());
}

TEST(MeshCut, PlaneOnFace_KeepsWholeCubeWithoutCap)
{
    const FractureMesh cube = UnitCube();
    const FractureMesh kept = Cut(cube, {1, 0, 0}, {1, 0, 0});
    EXPECT_TRUE(IsWatertight(kept));
    EXPECT_NEAR(ComputeVolume(kept), 1.0, kVolumeTolerance);
    EXPECT_EQ(ComputeInteriorArea(kept), 0.0);
    EXPECT_TRUE(Cut(cube, {1, 0, 0}, {-1, 0, 0}).triangles.empty());
}

TEST(MeshCut, PlaneThroughVertices_DiagonalHalf)
{
    const FractureMesh half = Cut(UnitCube(), {1, 0, 0}, {1, 1, 0});
    EXPECT_TRUE(IsWatertight(half));
    EXPECT_NEAR(ComputeVolume(half), 0.5, kVolumeTolerance);
    EXPECT_NEAR(ComputeInteriorArea(half), std::sqrt(2.0), 1e-5);
}

TEST(MeshCut, LShapeAlongZ_NonConvexCap)
{
    const FractureMesh bottom = Cut(MakeLShape(), {0, 0, 0.25}, {0, 0, 1});
    EXPECT_TRUE(IsWatertight(bottom));
    EXPECT_NEAR(ComputeVolume(bottom), 0.75, kVolumeTolerance);
    EXPECT_NEAR(ComputeInteriorArea(bottom), 3.0, 1e-5);
}

TEST(MeshCut, UShapeAcrossArms_TwoComponents)
{
    const FractureMesh top = Cut(MakeUShape(), {0, 1.5, 0}, {0, -1, 0});
    EXPECT_TRUE(IsWatertight(top));
    EXPECT_NEAR(ComputeVolume(top), 1.0, kVolumeTolerance);
    const auto pieces = SplitConnectedComponents(top);
    ASSERT_EQ(pieces.size(), 2u);
    for (const FractureMesh& piece : pieces)
    {
        EXPECT_TRUE(IsWatertight(piece));
        EXPECT_NEAR(ComputeVolume(piece), 0.5, kVolumeTolerance);
    }
}

TEST(MeshCut, TubeAcrossAxis_CapWithHole)
{
    const FractureMesh bottom = Cut(MakeTube(), {0, 0, 0.5}, {0, 0, 1});
    EXPECT_TRUE(IsWatertight(bottom));
    EXPECT_NEAR(ComputeVolume(bottom), 4.0, kVolumeTolerance);
    EXPECT_NEAR(ComputeInteriorArea(bottom), 8.0, 1e-5);
    EXPECT_FALSE(ContainsPoint(bottom, {1.5f, 1.5f, 0.25f}));
}

TEST(MeshCut, SuccessiveObliqueCuts_StayWatertight)
{
    FractureMesh mesh = MakeLShape();
    const std::array<std::array<double, 6>, 5> planes{{{1.2, 0.3, 0.5, 0.7, 0.2, 0.1},
                                                       {0.4, 1.1, 0.5, -0.3, 0.9, 0.2},
                                                       {0.5, 0.5, 0.8, 0.1, -0.2, 1.0},
                                                       {0.1, 0.2, 0.3, -1.0, -0.4, -0.3},
                                                       {0.9, 0.9, 0.1, 0.5, 0.5, -1.0}}};
    double previous = ComputeVolume(mesh);
    for (const auto& p : planes)
    {
        mesh = Cut(mesh, {p[0], p[1], p[2]}, {p[3], p[4], p[5]});
        ASSERT_TRUE(IsWatertight(mesh));
        const double volume = ComputeVolume(mesh);
        EXPECT_GT(volume, 0.0);
        EXPECT_LE(volume, previous + kVolumeTolerance);
        previous = volume;
    }
}

TEST(MeshCut, BothSides_VolumesSumToOriginal)
{
    const FractureMesh l = MakeLShape();
    const std::array<double, 3> point{0.8, 1.1, 0.3};
    const std::array<double, 3> normal{0.3, 0.8, -0.5};
    const FractureMesh a = Cut(l, point, normal);
    const FractureMesh b = Cut(l, point, {-normal[0], -normal[1], -normal[2]});
    EXPECT_NEAR(ComputeVolume(a) + ComputeVolume(b), 3.0, kVolumeTolerance);
    EXPECT_NEAR(ComputeInteriorArea(a), ComputeInteriorArea(b), 1e-5);
}

TEST(MeshCut, SameInput_IdenticalOutput)
{
    const FractureMesh l = MakeLShape();
    EXPECT_EQ(Cut(l, {0.8, 1.1, 0.3}, {0.3, 0.8, -0.5}), Cut(l, {0.8, 1.1, 0.3}, {0.3, 0.8, -0.5}));
}

TEST(MeshCut, CutPoints_InterpolateExteriorUvs)
{
    // The -z face of the unit cube has uv = (x, y); a cut at x = 0.25 must put its new corners at u = 0.25.
    const FractureMesh piece = Cut(UnitCube(), {0.25, 0, 0}, {1, 0, 0});
    for (const FractureTriangle& t : piece.triangles)
    {
        if (t.interior || t.normals[0] != Vec3(0, 0, -1))
        {
            continue;
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            EXPECT_FLOAT_EQ(t.uvs[k].u, piece.positions[t.indices[k]].x);
        }
    }
}
