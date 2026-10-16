// SPDX-License-Identifier: MIT
#include <interi/destruction_render/ChunkGatherer.h>

#include <gtest/gtest.h>

using namespace interi::destruction;

TEST(ChunkMaterials, Get_InteriorAndSurfaceTablesAreSeparate)
{
    ChunkMaterials materials;
    materials.surface.push_back({.baseColor = {1, 0, 0}});
    materials.interior.push_back({.baseColor = {0, 1, 0}});
    EXPECT_EQ(materials.Get({.surfaceMaterial = 0, .interior = false}).baseColor.x, 1.0f);
    EXPECT_EQ(materials.Get({.surfaceMaterial = 0, .interior = true}).baseColor.y, 1.0f);
}

TEST(ChunkMaterials, Get_OutOfRange_Fallback)
{
    ChunkMaterials materials;
    materials.fallback.baseColor = {0, 0, 1};
    EXPECT_EQ(materials.Get({.surfaceMaterial = 3, .interior = false}).baseColor.z, 1.0f);
    EXPECT_EQ(materials.Get({.surfaceMaterial = 0, .interior = true}).baseColor.z, 1.0f);
}
