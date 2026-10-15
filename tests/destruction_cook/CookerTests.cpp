// SPDX-License-Identifier: MIT
#include <interi/destruction_cook/Cooker.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include "GltfTestUtil.h"
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <set>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::ErrorCode;
using interi::JobSystem;
using interi::JobSystemDesc;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

std::vector<char> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// A slab with a wall standing on it and a terrain box under the slab, plus a sidecar config.
class CookerTest : public ::testing::Test
{
protected:
    CookerTest()
    {
        const std::vector<GltfBox> boxes{
            {.name = "Slab", .min = {0, 0, 0}, .max = {4, 0.3f, 4}, .material = 0},
            {.name = "Wall", .min = {0, 0.3f, 0}, .max = {4, 2.3f, 0.3f}, .material = 0},
            {.name = "Terrain", .min = {-2, -1, -2}, .max = {6, 0, 6}},
        };
        const std::vector<std::string> materials{"Concrete"};
        WriteBoxesGltf(Input(), boxes, materials);
        WriteConfig(4);
    }

    [[nodiscard]] std::filesystem::path Input() const { return m_dir.Path() / "house.gltf"; }
    [[nodiscard]] std::filesystem::path Output() const { return m_dir.Path() / "house.idest"; }

    void WriteConfig(int pieces) const
    {
        std::ofstream(m_dir.Path() / "house.fracture.json")
            << std::format(R"({{ "materialsFile": "{}",
                 "defaults": {{ "levels": [ {{ "count": {} }} ] }},
                 "objects": {{ "Terrain": {{ "anchor": true }} }} }})",
                           std::filesystem::path(INTERI_TEST_MATERIALS_FILE).generic_string(), pieces);
    }

    [[nodiscard]] CookRequest Request() const
    {
        CookRequest request;
        request.input = Input();
        return request;
    }

    interi::testing::ScopedTempDir m_dir;
    JobSystem m_jobs{Workers(2)};
};

} // namespace

TEST_F(CookerTest, Cook_WritesLoadableAssetWithAnchorsAndBonds)
{
    const auto report = Cook(Request(), m_jobs);
    ASSERT_TRUE(report.has_value()) << report.error().message;
    EXPECT_FALSE(report->skipped);
    EXPECT_EQ(report->output, Output());
    EXPECT_GT(report->chunkCount, 2u);
    EXPECT_GT(report->bondCount, 0u);
    EXPECT_GT(report->anchorCount, 0u);

    const auto asset = LoadDestructibleAsset(Output());
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->chunks.size(), report->chunkCount);
    EXPECT_EQ(asset->chunks[0].level, 0u); // the slab, whole
    for (const std::uint32_t anchor : asset->anchors)
    {
        EXPECT_EQ(asset->chunks[asset->chunks[anchor].parent].parent, kNoChunk) << "anchors are slab pieces";
    }
}

TEST_F(CookerTest, Cooker_UnchangedInput_SkipsRecook)
{
    const auto first = Cook(Request(), m_jobs);
    ASSERT_TRUE(first.has_value()) << first.error().message;
    const auto bytes = ReadBytes(Output());

    const auto second = Cook(Request(), m_jobs);
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->skipped);
    EXPECT_EQ(second->cookKey, first->cookKey);
    EXPECT_EQ(ReadBytes(Output()), bytes);
}

TEST_F(CookerTest, ChangedConfig_Recooks)
{
    const auto first = Cook(Request(), m_jobs);
    ASSERT_TRUE(first.has_value());
    WriteConfig(6);
    const auto second = Cook(Request(), m_jobs);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second->skipped);
    EXPECT_NE(second->cookKey, first->cookKey);
}

TEST_F(CookerTest, Force_Recooks)
{
    ASSERT_TRUE(Cook(Request(), m_jobs).has_value());
    CookRequest request = Request();
    request.force = true;
    const auto report = Cook(request, m_jobs);
    ASSERT_TRUE(report.has_value());
    EXPECT_FALSE(report->skipped);
}

TEST_F(CookerTest, Cooker_CorruptFile_ReportsError)
{
    // A corrupt cooked asset is reported on load and is recooked rather than trusted.
    ASSERT_TRUE(Cook(Request(), m_jobs).has_value());
    auto bytes = ReadBytes(Output());
    bytes[bytes.size() / 2] = static_cast<char>(bytes[bytes.size() / 2] ^ 0x55);
    std::ofstream(Output(), std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto corrupt = LoadDestructibleAsset(Output());
    ASSERT_FALSE(corrupt.has_value());
    EXPECT_EQ(corrupt.error().code, ErrorCode::Corrupt);

    std::ofstream(Output(), std::ios::binary | std::ios::trunc) << "garbage";
    const auto recooked = Cook(Request(), m_jobs);
    ASSERT_TRUE(recooked.has_value());
    EXPECT_FALSE(recooked->skipped);
    EXPECT_TRUE(LoadDestructibleAsset(Output()).has_value());

    // A corrupt source glTF is an error.
    std::ofstream(Input(), std::ios::trunc) << R"({ "asset": )";
    const auto failed = Cook(Request(), m_jobs);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::Corrupt);
}

TEST_F(CookerTest, UnknownMaterial_InvalidArgument)
{
    std::ofstream(m_dir.Path() / "house.fracture.json")
        << std::format(R"({{ "materialsFile": "{}", "defaults": {{ "material": "cheese" }} }})",
                       std::filesystem::path(INTERI_TEST_MATERIALS_FILE).generic_string());
    const auto report = Cook(Request(), m_jobs);
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::InvalidArgument);
}

TEST_F(CookerTest, ThreadCount_IdenticalOutput)
{
    JobSystem serial(Workers(0));
    CookRequest request = Request();
    request.output = m_dir.Path() / "serial.idest";
    ASSERT_TRUE(Cook(request, serial).has_value());
    request.output = m_dir.Path() / "parallel.idest";
    JobSystem parallel(Workers(6));
    ASSERT_TRUE(Cook(request, parallel).has_value());
    EXPECT_EQ(ReadBytes(m_dir.Path() / "serial.idest"), ReadBytes(m_dir.Path() / "parallel.idest"));
}

TEST(RenderMeshOptimize, KeepsTrianglesAndDropsUnusedVertices)
{
    RenderMesh mesh;
    for (int i = 0; i < 5; ++i)
    {
        mesh.vertices.push_back({.position = {static_cast<float>(i), 0, 0}, .normal = {0, 1, 0}, .uv = {}});
    }
    mesh.indices = {0, 1, 2, 2, 1, 3}; // vertex 4 unused
    mesh.submeshes = {{.surfaceMaterial = 0, .indexOffset = 0, .indexCount = 6}};
    const RenderMesh before = mesh;
    OptimizeRenderMesh(mesh);
    EXPECT_EQ(mesh.vertices.size(), 4u);
    ASSERT_EQ(mesh.indices.size(), 6u);
    std::multiset<float> beforeTriangles;
    std::multiset<float> afterTriangles;
    for (std::size_t t = 0; t < 6; t += 3)
    {
        beforeTriangles.insert(before.vertices[before.indices[t]].position.x +
                               before.vertices[before.indices[t + 1]].position.x * 10 +
                               before.vertices[before.indices[t + 2]].position.x * 100);
    }
    for (std::size_t t = 0; t < 6; t += 3)
    {
        // Rotations of a triangle keep its winding; compare the rotation starting at its smallest x.
        std::array<float, 3> x{mesh.vertices[mesh.indices[t]].position.x, mesh.vertices[mesh.indices[t + 1]].position.x,
                               mesh.vertices[mesh.indices[t + 2]].position.x};
        std::array<float, 3> original{};
        for (std::size_t r = 0; r < 3; ++r)
        {
            std::array<float, 3> candidate{x[r], x[(r + 1) % 3], x[(r + 2) % 3]};
            if (beforeTriangles.contains(candidate[0] + candidate[1] * 10 + candidate[2] * 100))
            {
                original = candidate;
            }
        }
        afterTriangles.insert(original[0] + original[1] * 10 + original[2] * 100);
    }
    EXPECT_EQ(afterTriangles, beforeTriangles);
}
