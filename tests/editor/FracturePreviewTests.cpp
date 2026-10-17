// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>
#include <interi/editor/FracturePreview.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include "../destruction_cook/GltfTestUtil.h"
#include <gtest/gtest.h>

#include <fstream>

using namespace interi;
using namespace interi::destruction;
using interi::editor::FracturePreview;

namespace
{

ImportedScene BoxScene(const Vec3& min, const Vec3& max)
{
    ImportedScene scene;
    scene.objects.push_back({.name = "Box", .mesh = MakeBoxMesh(min, max), .firstMaterialName = ""});
    scene.surfaceMaterials = {"default"};
    return scene;
}

CookConfig Pieces(std::uint32_t count)
{
    CookConfig config = DefaultCookConfig();
    config.defaults.levels[0].count = count;
    return config;
}

std::vector<DestructionMaterial> Concrete()
{
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    return materials;
}

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

} // namespace

TEST(FracturePreview, ParamsChange_RecooksOnlyAffectedAsset)
{
    JobSystem jobs(Workers(2));
    FracturePreview preview;
    preview.Add("wall", BoxScene({-2, 0, -0.15f}, {2, 3, 0.15f}), Pieces(6), Concrete());
    preview.Add("pillar", BoxScene({-0.5f, 0, -0.5f}, {0.5f, 4, 0.5f}), Pieces(4), Concrete());
    EXPECT_EQ(preview.Update(jobs), 2u);
    EXPECT_EQ(preview.Update(jobs), 0u); // nothing changed

    FracturePreview::Entry& wall = *preview.Find("wall");
    FracturePreview::Entry& pillar = *preview.Find("pillar");
    ASSERT_TRUE(wall.asset && pillar.asset);
    const auto pillarAsset = pillar.asset;
    const std::size_t wallChunks = wall.asset->chunks.size();

    wall.config.defaults.levels[0].count = 12;
    EXPECT_EQ(preview.Update(jobs), 1u);
    EXPECT_EQ(wall.revision, 2u);
    EXPECT_EQ(pillar.revision, 1u);
    EXPECT_EQ(pillar.asset, pillarAsset); // untouched
    EXPECT_GT(wall.asset->chunks.size(), wallChunks);
    EXPECT_TRUE(wall.error.empty());

    // A failing cook keeps the last good asset and reports why.
    const auto lastGood = wall.asset;
    wall.config.defaults.material = "cheese";
    EXPECT_EQ(preview.Update(jobs), 1u);
    EXPECT_EQ(wall.asset, lastGood);
    EXPECT_FALSE(wall.error.empty());
}

TEST(FracturePreview, OpenSaveAndCook_WritesSidecarAndAsset)
{
    const interi::testing::ScopedTempDir dir;
    const std::vector<destruction::testing::GltfBox> boxes{
        {.name = "Wall", .min = {0, 0, 0}, .max = {4, 2, 0.3f}, .material = 0}};
    const std::vector<std::string> surfaces{"Concrete"};
    destruction::testing::WriteBoxesGltf(dir.Path() / "wall.gltf", boxes, surfaces);
    JobSystem jobs(Workers(2));
    FracturePreview preview;
    ASSERT_TRUE(preview.Open("wall", dir.Path() / "wall.gltf", INTERI_TEST_MATERIALS_FILE).has_value());
    FracturePreview::Entry& entry = *preview.Find("wall");
    EXPECT_FALSE(entry.config.materialsFile.empty()); // the default materials, relative to the glTF
    entry.config.defaults.levels[0].count = 5;
    ASSERT_EQ(preview.Update(jobs), 1u);
    ASSERT_TRUE(entry.asset != nullptr) << entry.error;

    const auto report = preview.SaveAndCook("wall", jobs);
    ASSERT_TRUE(report.has_value()) << report.error().message;
    EXPECT_TRUE(std::filesystem::exists(dir.Path() / "wall.fracture.json"));
    const auto cooked = LoadDestructibleAsset(dir.Path() / "wall.idest");
    ASSERT_TRUE(cooked.has_value());
    EXPECT_EQ(*cooked, *entry.asset); // the preview is what the cooker writes

    // Reopening reads the saved sidecar.
    FracturePreview reopened;
    ASSERT_TRUE(reopened.Open("wall", dir.Path() / "wall.gltf", INTERI_TEST_MATERIALS_FILE).has_value());
    EXPECT_EQ(reopened.Find("wall")->config.defaults.levels[0].count, 5u);
    EXPECT_FALSE(preview.SaveAndCook("missing", jobs).has_value());
}
