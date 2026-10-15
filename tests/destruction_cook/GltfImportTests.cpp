// SPDX-License-Identifier: MIT
#include <interi/destruction_cook/GltfImport.h>
#include <interi/testing/ScopedTempDir.h>

#include "GltfTestUtil.h"
#include <gtest/gtest.h>

#include <fstream>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::ErrorCode;
using interi::Vec3;

} // namespace

TEST(GltfImport, TwoBoxes_WorldSpaceWeldedObjects)
{
    interi::testing::ScopedTempDir dir;
    const auto path = dir.Path() / "boxes.gltf";
    const std::vector<GltfBox> boxes{
        {.name = "A", .min = {0, 0, 0}, .max = {1, 1, 1}, .material = 0},
        {.name = "B", .min = {0, 0, 0}, .max = {1, 1, 1}, .material = -1, .translation = {5, 0, 0}, .scale = {2, 1, 1}},
    };
    const std::vector<std::string> materials{"Brick"};
    WriteBoxesGltf(path, boxes, materials);

    const auto scene = ImportGltf(path);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    ASSERT_EQ(scene->objects.size(), 2u);
    EXPECT_EQ(scene->surfaceMaterials, (std::vector<std::string>{"Brick", "default"}));

    const SourceObject& a = scene->objects[0];
    EXPECT_EQ(a.name, "A");
    EXPECT_EQ(a.firstMaterialName, "Brick");
    EXPECT_EQ(a.mesh.positions.size(), 8u);
    EXPECT_TRUE(IsWatertight(a.mesh));
    EXPECT_NEAR(ComputeVolume(a.mesh), 1.0, 1e-6);
    EXPECT_EQ(a.mesh.triangles[0].surfaceMaterial, 0u);

    const SourceObject& b = scene->objects[1];
    EXPECT_EQ(b.firstMaterialName, "");
    EXPECT_EQ(b.mesh.triangles[0].surfaceMaterial, 1u); // "default"
    EXPECT_NEAR(ComputeVolume(b.mesh), 2.0, 1e-6);
    const interi::AABB bounds = ComputeBounds(b.mesh);
    EXPECT_EQ(bounds.min, Vec3(5, 0, 0));
    EXPECT_EQ(bounds.max, Vec3(7, 1, 1));
}

TEST(GltfImport, MirroredNode_KeepsOutwardWindingAndNormals)
{
    interi::testing::ScopedTempDir dir;
    const auto path = dir.Path() / "mirrored.gltf";
    const std::vector<GltfBox> boxes{{.name = "M", .min = {0, 0, 0}, .max = {1, 1, 1}, .scale = {-1, 1, 1}}};
    WriteBoxesGltf(path, boxes);
    const auto scene = ImportGltf(path);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;
    const FractureMesh& mesh = scene->objects[0].mesh;
    EXPECT_NEAR(ComputeVolume(mesh), 1.0, 1e-6); // positive: still outward-facing
    const Vec3 centroid = ComputeCentroid(mesh);
    for (const FractureTriangle& t : mesh.triangles)
    {
        const Vec3 toFace = mesh.positions[t.indices[0]] - centroid;
        EXPECT_GT(interi::Dot(t.normals[0], toFace), 0.0f);
    }
}

TEST(GltfImport, MissingFile_IoError)
{
    const auto scene = ImportGltf("no/such/file.gltf");
    ASSERT_FALSE(scene.has_value());
    EXPECT_EQ(scene.error().code, ErrorCode::IoError);
}

TEST(GltfImport, Garbage_Corrupt)
{
    interi::testing::ScopedTempDir dir;
    const auto path = dir.Path() / "garbage.gltf";
    std::ofstream(path) << "{ this is not gltf";
    const auto scene = ImportGltf(path);
    ASSERT_FALSE(scene.has_value());
    EXPECT_EQ(scene.error().code, ErrorCode::Corrupt);
}

TEST(GltfImport, SameFile_SameContentHash)
{
    interi::testing::ScopedTempDir dir;
    const std::vector<GltfBox> boxes{{.name = "A", .min = {0, 0, 0}, .max = {1, 1, 1}}};
    WriteBoxesGltf(dir.Path() / "a.gltf", boxes);
    WriteBoxesGltf(dir.Path() / "b.gltf", boxes);
    const std::vector<GltfBox> other{{.name = "A", .min = {0, 0, 0}, .max = {1, 2, 1}}};
    WriteBoxesGltf(dir.Path() / "c.gltf", other);
    EXPECT_EQ(ImportGltf(dir.Path() / "a.gltf")->contentHash, ImportGltf(dir.Path() / "b.gltf")->contentHash);
    EXPECT_NE(ImportGltf(dir.Path() / "a.gltf")->contentHash, ImportGltf(dir.Path() / "c.gltf")->contentHash);
}
