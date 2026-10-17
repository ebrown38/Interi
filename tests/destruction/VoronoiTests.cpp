// SPDX-License-Identifier: MIT
#include <interi/destruction/Voronoi.h>
#include <interi/jobs/JobSystem.h>

#include "DestructionTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Vec3;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

FractureMesh UnitCube()
{
    return MakeBoxMesh({0, 0, 0}, {1, 1, 1});
}

double TotalVolume(const FractureResult& result)
{
    double volume = 0.0;
    for (const FracturePiece& piece : result.pieces)
    {
        volume += piece.volume;
    }
    return volume;
}

std::vector<Vec3> UniformSeeds(const FractureMesh& mesh, std::uint32_t count, std::uint64_t randomSeed = 7)
{
    SeedSettings settings;
    settings.count = count;
    settings.randomSeed = randomSeed;
    return GenerateSeeds(mesh, settings);
}

} // namespace

TEST(Seeds, Uniform_CountInsideMeshAndDeterministic)
{
    const FractureMesh l = MakeLShape();
    const auto seeds = UniformSeeds(l, 50);
    EXPECT_EQ(seeds.size(), 50u);
    for (const Vec3& s : seeds)
    {
        EXPECT_TRUE(ContainsPoint(l, s));
    }
    EXPECT_EQ(seeds, UniformSeeds(l, 50));
    EXPECT_NE(seeds, UniformSeeds(l, 50, 8));
}

TEST(Seeds, Clustered_DenserNearImpact)
{
    const FractureMesh box = MakeBoxMesh({-2, -2, -2}, {2, 2, 2});
    const auto seeds = GenerateSeeds(
        box, {.pattern = SeedPattern::Clustered, .count = 400, .impactPoint = {0, 0, 0}, .clusterRadius = 2.0f});
    ASSERT_EQ(seeds.size(), 400u);
    // Uniform radius: half of the points within r = 1, which holds only 1/8 of the sphere's volume.
    const auto inner = std::ranges::count_if(seeds, [](const Vec3& s) { return interi::Length(s) < 1.0f; });
    EXPECT_GT(inner, 150);
    for (const Vec3& s : seeds)
    {
        EXPECT_LE(interi::Length(s), 2.0f + 1e-5f);
    }
}

TEST(Seeds, Grid_OnePerCellForPlanks)
{
    const FractureMesh beam = MakeBoxMesh({0, 0, 0}, {0.4f, 0.4f, 4.0f});
    SeedSettings settings;
    settings.pattern = SeedPattern::Grid;
    settings.randomSeed = 3;
    settings.gridCellSize = {0.2f, 0.2f, 2.0f};
    settings.jitter = 0.2f;
    const auto seeds = GenerateSeeds(beam, settings);
    EXPECT_EQ(seeds.size(), 8u);
}

TEST(SeedPaint, Density_IncreasesLocalSeeds)
{
    const FractureMesh wall = MakeBoxMesh({-2, 0, -0.15f}, {2, 3, 0.15f});
    const Vec3 corner{-1.4f, 0.6f, 0.0f};
    const auto near = [&](const std::vector<Vec3>& seeds)
    { return std::ranges::count_if(seeds, [&](const Vec3& s) { return interi::Length(s - corner) <= 0.5f; }); };

    SeedSettings uniform;
    uniform.count = 20;
    uniform.randomSeed = 7;
    SeedSettings painted = uniform;
    painted.pattern = SeedPattern::Painted;
    painted.strokes.push_back({.centre = corner, .radius = 0.5f, .density = 400.0f});
    const auto base = GenerateSeeds(wall, uniform);
    const auto seeds = GenerateSeeds(wall, painted);

    // The base points are the uniform ones; the stroke adds about density x (disc of r = 0.5 through 0.3 m) = 94.
    ASSERT_GE(seeds.size(), base.size());
    EXPECT_TRUE(std::equal(base.begin(), base.end(), seeds.begin()));
    const auto added = near(seeds) - near(base);
    EXPECT_GT(added, 60);
    EXPECT_LT(added, 130);
    for (std::size_t i = base.size(); i < seeds.size(); ++i)
    {
        EXPECT_LE(interi::Length(seeds[i] - corner), 0.5f + 1e-5f);
        EXPECT_TRUE(ContainsPoint(wall, seeds[i]));
    }
    EXPECT_EQ(seeds, GenerateSeeds(wall, painted)); // deterministic

    // Editing one stroke leaves another's points alone.
    painted.strokes.push_back({.centre = {1.0f, 2.0f, 0.0f}, .radius = 0.3f, .density = 300.0f});
    const auto two = GenerateSeeds(wall, painted);
    painted.strokes[1].radius = 0.4f;
    const auto edited = GenerateSeeds(wall, painted);
    const std::size_t first = seeds.size();
    ASSERT_GE(two.size(), first);
    EXPECT_TRUE(std::equal(seeds.begin(), seeds.end(), two.begin()));
    EXPECT_TRUE(std::equal(seeds.begin(), seeds.end(), edited.begin()));
    EXPECT_NE(two, edited);
}

TEST(SeedPaint, StrokeOutsideMesh_AddsNothing)
{
    const FractureMesh cube = UnitCube();
    SeedSettings painted;
    painted.pattern = SeedPattern::Painted;
    painted.count = 5;
    painted.strokes.push_back({.centre = {5, 5, 5}, .radius = 1.0f, .density = 1000.0f});
    painted.strokes.push_back({.centre = {0.5f, 0.5f, 0.5f}, .radius = 0.0f, .density = 1000.0f});
    EXPECT_EQ(GenerateSeeds(cube, painted).size(), 5u);
}

TEST(Voronoi, CubeOneSeed_ReturnsCube)
{
    JobSystem jobs(Workers(0));
    const FractureMesh cube = UnitCube();
    const std::vector<Vec3> seeds{{0.3f, 0.6f, 0.5f}};
    const FractureResult result = VoronoiFracture(cube, seeds, {}, jobs);
    ASSERT_EQ(result.pieces.size(), 1u);
    EXPECT_TRUE(IsWatertight(result.pieces[0].mesh));
    EXPECT_NEAR(result.pieces[0].volume, 1.0, 1e-6);
    EXPECT_EQ(result.pieces[0].mesh.triangles.size(), cube.triangles.size());
}

TEST(Voronoi, CubeNSeeds_VolumeSumEqualsOriginal)
{
    JobSystem jobs(Workers(2));
    const FractureMesh cube = UnitCube();
    const FractureResult result = VoronoiFracture(cube, UniformSeeds(cube, 40), {}, jobs);
    EXPECT_EQ(result.pieces.size(), 40u);
    EXPECT_NEAR(TotalVolume(result), 1.0, 1e-3); // within 0.1%
}

TEST(Voronoi, CellsWatertight)
{
    JobSystem jobs(Workers(2));
    const FractureMesh l = MakeLShape();
    const FractureResult result = VoronoiFracture(l, UniformSeeds(l, 30), {}, jobs);
    ASSERT_FALSE(result.pieces.empty());
    for (const FracturePiece& piece : result.pieces)
    {
        EXPECT_TRUE(IsWatertight(piece.mesh)) << "piece of seed " << piece.seed;
        EXPECT_GT(piece.volume, 0.0);
    }
}

TEST(Voronoi, SameSeed_IdenticalOutput)
{
    JobSystem jobs(Workers(2));
    const FractureMesh l = MakeLShape();
    const FractureResult a = VoronoiFracture(l, UniformSeeds(l, 25), {}, jobs);
    const FractureResult b = VoronoiFracture(l, UniformSeeds(l, 25), {}, jobs);
    ASSERT_EQ(a.pieces.size(), b.pieces.size());
    for (std::size_t i = 0; i < a.pieces.size(); ++i)
    {
        EXPECT_EQ(a.pieces[i].mesh, b.pieces[i].mesh);
        EXPECT_EQ(a.pieces[i].seed, b.pieces[i].seed);
    }
}

TEST(Voronoi, SingleThreadVsParallel_IdenticalOutput)
{
    const FractureMesh u = MakeUShape();
    const auto seeds = UniformSeeds(u, 40);
    const VoronoiSettings settings{.minPieceVolume = 0.01};
    JobSystem serial(Workers(0));
    JobSystem parallel(Workers(7));
    const FractureResult a = VoronoiFracture(u, seeds, settings, serial);
    const FractureResult b = VoronoiFracture(u, seeds, settings, parallel);
    ASSERT_EQ(a.pieces.size(), b.pieces.size());
    EXPECT_EQ(a.mergedSeeds, b.mergedSeeds);
    for (std::size_t i = 0; i < a.pieces.size(); ++i)
    {
        EXPECT_EQ(a.pieces[i].mesh, b.pieces[i].mesh);
    }
}

TEST(Voronoi, SliverBelowThreshold_Merged)
{
    JobSystem jobs(Workers(0));
    const FractureMesh cube = UnitCube();
    // The seed outside the cube only reaches a tiny tetrahedron at the corner (1, 1, 1).
    const std::vector<Vec3> seeds{{0.3f, 0.3f, 0.3f}, {0.6f, 0.3f, 0.3f}, {1.6f, 1.6f, 1.6f}};
    const FractureResult unmerged = VoronoiFracture(cube, seeds, {}, jobs);
    ASSERT_EQ(unmerged.pieces.size(), 3u);
    EXPECT_LT(unmerged.pieces[2].volume, 0.001);

    const FractureResult merged = VoronoiFracture(cube, seeds, {.minPieceVolume = 0.01}, jobs);
    EXPECT_EQ(merged.mergedSeeds, 1u);
    ASSERT_EQ(merged.pieces.size(), 2u);
    EXPECT_NEAR(TotalVolume(merged), 1.0, 1e-5);
    for (const FracturePiece& piece : merged.pieces)
    {
        EXPECT_GE(piece.volume, 0.01);
        EXPECT_TRUE(IsWatertight(piece.mesh));
    }
}

TEST(Voronoi, NonConvexLShape_NoCellOutsideSource)
{
    JobSystem jobs(Workers(2));
    const FractureMesh l = MakeLShape();
    const FractureResult result = VoronoiFracture(l, UniformSeeds(l, 30), {}, jobs);
    EXPECT_NEAR(TotalVolume(result), 3.0, 3e-3);
    constexpr float kEps = 1e-5f;
    for (const FracturePiece& piece : result.pieces)
    {
        for (const Vec3& p : piece.mesh.positions)
        {
            EXPECT_TRUE(p.x >= -kEps && p.x <= 2 + kEps && p.y >= -kEps && p.y <= 2 + kEps && p.z >= -kEps &&
                        p.z <= 1 + kEps);
            EXPECT_FALSE(p.x > 1 + kEps && p.y > 1 + kEps) << "vertex in the notch";
        }
    }
}

TEST(Voronoi, UShapeSeedAcrossNotch_CellSplitIntoPieces)
{
    JobSystem jobs(Workers(0));
    const FractureMesh u = MakeUShape();
    // Seed 0 sits above the notch (outside the mesh): its cell takes the inner top corners of both arms, which
    // are not connected inside the mesh.
    const std::vector<Vec3> seeds{{1.5f, 2.5f, 0.5f}, {0.2f, 1.8f, 0.5f}, {2.8f, 1.8f, 0.5f}, {1.5f, 0.3f, 0.5f}};
    const FractureResult result = VoronoiFracture(u, seeds, {}, jobs);
    EXPECT_NEAR(TotalVolume(result), 5.0, 1e-4);
    std::size_t piecesOfSeed0 = 0;
    for (const FracturePiece& piece : result.pieces)
    {
        EXPECT_EQ(SplitConnectedComponents(piece.mesh).size(), 1u);
        piecesOfSeed0 += piece.seed == 0 ? 1 : 0;
    }
    EXPECT_EQ(piecesOfSeed0, 2u);
}

TEST(Voronoi, MultiPartInput_PiecesNeverSpanParts)
{
    JobSystem jobs(Workers(2));
    FractureMesh twoBoxes = MakeBoxMesh({0, 0, 0}, {1, 1, 1});
    AppendMesh(twoBoxes, MakeBoxMesh({1.5f, 0, 0}, {2.5f, 1, 1}));
    const FractureResult result = VoronoiFracture(twoBoxes, UniformSeeds(twoBoxes, 12), {}, jobs);
    EXPECT_NEAR(TotalVolume(result), 2.0, 2e-3);
    for (const FracturePiece& piece : result.pieces)
    {
        const interi::AABB bounds = ComputeBounds(piece.mesh);
        EXPECT_TRUE(bounds.max.x <= 1.0f + 1e-5f || bounds.min.x >= 1.5f - 1e-5f);
        EXPECT_TRUE(IsWatertight(piece.mesh));
    }
}

TEST(Voronoi, DuplicateSeeds_Ignored)
{
    JobSystem jobs(Workers(0));
    const std::vector<Vec3> seeds{{0.25f, 0.5f, 0.5f}, {0.75f, 0.5f, 0.5f}, {0.25f, 0.5f, 0.5f}};
    const FractureResult result = VoronoiFracture(UnitCube(), seeds, {}, jobs);
    ASSERT_EQ(result.pieces.size(), 2u);
    EXPECT_NEAR(result.pieces[0].volume, 0.5, 1e-6);
}

TEST(Voronoi, ManySeedsInTube_WatertightAndVolumeConserved)
{
    JobSystem jobs(Workers(4));
    const FractureMesh tube = MakeTube();
    const FractureResult result = VoronoiFracture(tube, UniformSeeds(tube, 150, 11), {}, jobs);
    EXPECT_NEAR(TotalVolume(result), 8.0, 8e-3);
    for (const FracturePiece& piece : result.pieces)
    {
        EXPECT_TRUE(IsWatertight(piece.mesh)) << "piece of seed " << piece.seed;
    }
}
