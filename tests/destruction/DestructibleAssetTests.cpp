// SPDX-License-Identifier: MIT
#include <interi/destruction/DestructibleAsset.h>
#include <interi/jobs/JobSystem.h>

#include "DestructionTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::ErrorCode;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::Vec3;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

std::vector<DestructionMaterial> Materials()
{
    return {DestructionMaterial{.name = "concrete", .interiorMaterialId = 9}};
}

DestructibleAsset MakeAsset(JobSystem& jobs)
{
    HierarchySettings settings;
    SeedSettings level;
    level.count = 6;
    settings.levels.push_back(level);
    const ChunkHierarchy hierarchy = BuildHierarchy(MakeLShape(), settings, jobs);
    BondSettings bondSettings;
    bondSettings.groundHeight = 0.0f;
    BondGraph bonds = BuildBondGraph(hierarchy.chunks, Materials(), bondSettings);
    auto asset = AssembleAsset(hierarchy.chunks, std::move(bonds), Materials(), {}, jobs);
    EXPECT_TRUE(asset.has_value()) << asset.error().message;
    return asset.value_or(DestructibleAsset{});
}

} // namespace

TEST(CollisionHulls, Cube_SingleHull)
{
    const auto hulls = BuildCollisionHulls(MakeBoxMesh({0, 0, 0}, {1, 2, 3}));
    ASSERT_TRUE(hulls.has_value());
    ASSERT_EQ(hulls->size(), 1u);
    EXPECT_EQ(hulls->front().points.size(), 8u);
    EXPECT_NEAR(ConvexHullVolume(hulls->front().points), 6.0, 1e-4);
}

TEST(CollisionHulls, LShape_SplitIntoTightHulls)
{
    const auto hulls = BuildCollisionHulls(MakeLShape());
    ASSERT_TRUE(hulls.has_value());
    EXPECT_GE(hulls->size(), 2u);
    double total = 0.0;
    for (const CollisionHull& hull : *hulls)
    {
        total += ConvexHullVolume(hull.points);
    }
    EXPECT_LE(total, 3.0 * 1.15 + 1e-6); // within maxConcavity of the true volume (one hull would be 3.5)
    EXPECT_GE(total, 3.0 - 1e-4);
}

TEST(CollisionHulls, DepthZero_OneHullPerComponent)
{
    FractureMesh twoBoxes = MakeBoxMesh({0, 0, 0}, {1, 1, 1});
    AppendMesh(twoBoxes, MakeBoxMesh({3, 0, 0}, {4, 1, 1}));
    const auto hulls = BuildCollisionHulls(twoBoxes, {.maxConcavity = 0.15, .maxDepth = 0});
    ASSERT_TRUE(hulls.has_value());
    EXPECT_EQ(hulls->size(), 2u);
}

TEST(CollisionHulls, FlatMesh_Degenerate)
{
    const auto hulls = BuildCollisionHulls(MakeBoxMesh({0, 0, 0}, {1, 1, 0}));
    ASSERT_FALSE(hulls.has_value());
    EXPECT_EQ(hulls.error().code, ErrorCode::Degenerate);
}

TEST(RenderMesh, Cube_SharesCornersPerFace)
{
    const RenderMesh render = BuildRenderMesh(MakeBoxMesh({0, 0, 0}, {1, 1, 1}), Materials());
    EXPECT_EQ(render.vertices.size(), 24u); // 4 corners x 6 faces (normals differ between faces)
    EXPECT_EQ(render.indices.size(), 36u);
    ASSERT_EQ(render.submeshes.size(), 1u);
    EXPECT_EQ(render.submeshes[0].indexCount, 36u);
}

TEST(RenderMesh, InteriorFaces_UseInteriorMaterial)
{
    const FractureMesh half = CutMesh(MakeBoxMesh({0, 0, 0}, {1, 1, 1}, 0), Plane({0.5, 0, 0}, {1, 0, 0}), 1e-6);
    const RenderMesh render = BuildRenderMesh(half, Materials());
    ASSERT_EQ(render.submeshes.size(), 2u);
    EXPECT_EQ(render.submeshes[0].surfaceMaterial, 0u);
    EXPECT_EQ(render.submeshes[1].surfaceMaterial, 9u);
    EXPECT_EQ(render.submeshes[1].indexOffset, render.submeshes[0].indexCount);
    EXPECT_FALSE(render.submeshes[0].interior);
    EXPECT_TRUE(render.submeshes[1].interior);
}

TEST(RenderMesh, InteriorIdEqualToSurfaceId_SeparateSubmeshes)
{
    // interiorMaterialId 0 and surface material 0 are different materials: the flag keeps them apart.
    const std::vector<DestructionMaterial> materials{DestructionMaterial{.name = "concrete", .interiorMaterialId = 0}};
    const FractureMesh half = CutMesh(MakeBoxMesh({0, 0, 0}, {1, 1, 1}, 0), Plane({0.5, 0, 0}, {1, 0, 0}), 1e-6);
    const RenderMesh render = BuildRenderMesh(half, materials);
    ASSERT_EQ(render.submeshes.size(), 2u);
    EXPECT_EQ(render.submeshes[0].surfaceMaterial, 0u);
    EXPECT_FALSE(render.submeshes[0].interior);
    EXPECT_EQ(render.submeshes[1].surfaceMaterial, 0u);
    EXPECT_TRUE(render.submeshes[1].interior);
}

TEST(DestructibleAsset, Assemble_MassFromDensityAndHullsPerChunk)
{
    JobSystem jobs(Workers(2));
    const DestructibleAsset asset = MakeAsset(jobs);
    ASSERT_GT(asset.chunks.size(), 1u);
    EXPECT_NEAR(asset.chunks[0].mass, 3.0f * 2400.0f, 1.0f);
    for (const AssetChunk& chunk : asset.chunks)
    {
        EXPECT_FALSE(chunk.hulls.empty());
        EXPECT_FALSE(chunk.render.indices.empty());
    }
    EXPECT_FALSE(asset.bonds.empty());
    EXPECT_FALSE(asset.anchors.empty());
}

TEST(DestructibleAsset, RoundTrip_Identical)
{
    JobSystem jobs(Workers(2));
    const DestructibleAsset asset = MakeAsset(jobs);
    const std::vector<std::byte> bytes = WriteDestructibleAsset(asset, 0x1234'5678'9ABC'DEF0ull);
    const auto loaded = ReadDestructibleAsset(bytes);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_EQ(*loaded, asset);
    EXPECT_EQ(WriteDestructibleAsset(*loaded, 0x1234'5678'9ABC'DEF0ull), bytes);

    const auto key = ReadCookKey(bytes);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, 0x1234'5678'9ABC'DEF0ull);
}

TEST(DestructibleAsset, FlippedPayloadByte_Corrupt)
{
    JobSystem jobs(Workers(0));
    std::vector<std::byte> bytes = WriteDestructibleAsset(MakeAsset(jobs), 1);
    bytes[bytes.size() / 2] ^= std::byte{0x10};
    const auto loaded = ReadDestructibleAsset(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, ErrorCode::Corrupt);
}

TEST(DestructibleAsset, Truncated_Corrupt)
{
    JobSystem jobs(Workers(0));
    std::vector<std::byte> bytes = WriteDestructibleAsset(MakeAsset(jobs), 1);
    for (const std::size_t size : {std::size_t{0}, std::size_t{6}, std::size_t{20}, bytes.size() - 1})
    {
        const auto loaded = ReadDestructibleAsset(std::span(bytes).first(size));
        ASSERT_FALSE(loaded.has_value()) << size;
        EXPECT_EQ(loaded.error().code, ErrorCode::Corrupt) << size;
    }
    EXPECT_FALSE(ReadCookKey(std::span(bytes).first(10)).has_value());
}

TEST(DestructibleAsset, NewerVersion_VersionMismatch)
{
    std::vector<std::byte> bytes = WriteDestructibleAsset({}, 1);
    bytes[4] = std::byte{static_cast<unsigned char>(kDestructibleVersion + 1)}; // u32 version after the magic
    const auto loaded = ReadDestructibleAsset(bytes);
    ASSERT_FALSE(loaded.has_value());
    EXPECT_EQ(loaded.error().code, ErrorCode::VersionMismatch);
}

TEST(DestructibleAsset, Version1Submesh_ReadsAsExterior)
{
    std::vector<std::byte> bytes;
    {
        interi::ArchiveWriter writer(bytes, kDestructiblePayloadMagic, 1);
        Submesh submesh{.surfaceMaterial = 3, .indexOffset = 6, .indexCount = 9, .interior = true};
        writer(submesh); // version 1 has no interior flag
    }
    auto reader = interi::ArchiveReader::Open(bytes, kDestructiblePayloadMagic, kDestructibleVersion);
    ASSERT_TRUE(reader.has_value());
    Submesh read;
    (*reader)(read);
    ASSERT_TRUE(reader->Ok());
    EXPECT_EQ(read, (Submesh{.surfaceMaterial = 3, .indexOffset = 6, .indexCount = 9, .interior = false}));
    EXPECT_EQ(reader->Remaining(), 0u);
}
