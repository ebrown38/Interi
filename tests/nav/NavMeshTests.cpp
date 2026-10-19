// SPDX-License-Identifier: MIT
#include <interi/nav/NavMesh.h>

#include "NavTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <thread>

using namespace interi;
using namespace interi::nav;
using namespace interi::nav::testing;

namespace
{

NavBakeSettings Settings()
{
    NavBakeSettings settings;
    settings.agentRadius = 0.3f;
    return settings;
}

NavMesh Bake(const Geometry& g)
{
    auto mesh = NavMesh::Bake(g.vertices, g.indices, Settings());
    EXPECT_TRUE(mesh.has_value()) << (mesh ? "" : mesh.error().message);
    return std::move(*mesh);
}

} // namespace

TEST(NavMesh, OpenFloor_PathIsAStraightLine)
{
    const NavMesh mesh = Bake(SimpleFloor());
    EXPECT_GT(mesh.PolygonCount(), 0u);
    const auto path = mesh.FindPath(Vec3(-5.0f, 0.0f, -5.0f), Vec3(5.0f, 0.0f, 5.0f));
    ASSERT_TRUE(path.has_value()) << path.error().message;
    EXPECT_FALSE(path->partial);
    EXPECT_EQ(path->points.size(), 2u);
    EXPECT_NEAR(path->Length(), std::sqrt(200.0f), 0.2f);
}

TEST(NavMesh, PitInTheMiddle_ForcesADetour)
{
    const NavMesh mesh = Bake(FloorWithPit());
    const auto path = mesh.FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    ASSERT_TRUE(path.has_value()) << path.error().message;
    EXPECT_FALSE(path->partial);
    EXPECT_GT(path->Length(), 13.0f); // the straight line is 12 m
    EXPECT_GT(path->points.size(), 2u);
    for (const Vec3& p : path->points)
    {
        const bool inPit = std::abs(p.x) < 2.9f && std::abs(p.z) < 2.9f;
        EXPECT_FALSE(inPit) << p.x << ", " << p.z;
    }
}

TEST(NavMesh, AgentRadius_KeepsPathsOffTheEdge)
{
    const NavMesh mesh = Bake(SimpleFloor());
    const auto nearest = mesh.NearestPoint(Vec3(9.9f, 0.0f, 0.0f));
    ASSERT_TRUE(nearest.has_value());
    EXPECT_LT(nearest->x, 9.9f); // the mesh is eroded by the agent radius
}

TEST(NavMesh, OffTheMesh_NotFound)
{
    const NavMesh mesh = Bake(SimpleFloor());
    const auto path = mesh.FindPath(Vec3(0.0f, 50.0f, 0.0f), Vec3(1.0f, 0.0f, 1.0f));
    ASSERT_FALSE(path.has_value());
    EXPECT_EQ(path.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(mesh.NearestPoint(Vec3(100.0f, 0.0f, 100.0f)).has_value());
}

TEST(NavMesh, UnreachableIsland_PartialPathToTheClosestPoint)
{
    const NavMesh mesh = Bake(TwoIslands());
    const auto path = mesh.FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    ASSERT_TRUE(path.has_value()) << path.error().message;
    EXPECT_TRUE(path->partial);
    EXPECT_LT(path->points.back().x, -1.0f);
}

TEST(NavMesh, BadInput_Rejected)
{
    const Geometry floor = SimpleFloor();
    EXPECT_EQ(NavMesh::Bake({}, {}, Settings()).error().code, ErrorCode::InvalidArgument);
    std::vector<std::uint32_t> outOfRange = floor.indices;
    outOfRange[0] = 99;
    EXPECT_EQ(NavMesh::Bake(floor.vertices, outOfRange, Settings()).error().code, ErrorCode::InvalidArgument);
    NavBakeSettings bad = Settings();
    bad.cellSize = 0.0f;
    EXPECT_EQ(NavMesh::Bake(floor.vertices, floor.indices, bad).error().code, ErrorCode::InvalidArgument);
    std::vector<Vec3> nan = floor.vertices;
    nan[0].x = std::nanf("");
    EXPECT_EQ(NavMesh::Bake(nan, floor.indices, Settings()).error().code, ErrorCode::InvalidArgument);
}

TEST(NavMesh, OnlyAWall_NothingWalkable)
{
    // A vertical quad is steeper than any slope an agent climbs.
    const std::vector<Vec3> wall = {Vec3(0, 0, 0), Vec3(0, 5, 0), Vec3(5, 5, 0), Vec3(5, 0, 0)};
    const std::vector<std::uint32_t> indices = {0, 1, 3, 1, 2, 3};
    const auto mesh = NavMesh::Bake(wall, indices, Settings());
    ASSERT_FALSE(mesh.has_value());
    EXPECT_EQ(mesh.error().code, ErrorCode::Degenerate);
}

TEST(NavMesh, WriteRead_GivesTheSamePaths)
{
    const NavMesh mesh = Bake(FloorWithPit());
    const std::vector<std::byte> bytes = mesh.Write();
    auto loaded = NavMesh::Read(bytes);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_EQ(loaded->PolygonCount(), mesh.PolygonCount());
    EXPECT_EQ(loaded->Write(), bytes);
    const auto a = mesh.FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    const auto b = loaded->FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->points, b->points);
}

TEST(NavMesh, Read_DamagedFile_Corrupt)
{
    const NavMesh mesh = Bake(SimpleFloor());
    std::vector<std::byte> bytes = mesh.Write();
    EXPECT_EQ(NavMesh::Read({}).error().code, ErrorCode::Corrupt);
    std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    EXPECT_FALSE(NavMesh::Read(truncated).has_value());
    bytes[bytes.size() - 5] ^= std::byte{0x5A};
    EXPECT_EQ(NavMesh::Read(bytes).error().code, ErrorCode::Corrupt);
}

TEST(NavMesh, ConcurrentQueries_AgreeWithSerial)
{
    const NavMesh mesh = Bake(FloorWithPit());
    const auto expected = mesh.FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
    ASSERT_TRUE(expected.has_value());
    std::vector<std::thread> threads;
    std::vector<int> ok(4, 0);
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [&, t]
            {
                for (int i = 0; i < 50; ++i)
                {
                    const auto path = mesh.FindPath(Vec3(-6.0f, 0.0f, 0.0f), Vec3(6.0f, 0.0f, 0.0f));
                    if (!path || path->points != expected->points)
                    {
                        return;
                    }
                }
                ok[static_cast<std::size_t>(t)] = 1;
            });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(ok, std::vector<int>(4, 1));
}
