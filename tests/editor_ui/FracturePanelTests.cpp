// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>
#include <interi/editor_ui/FracturePanel.h>
#include <interi/jobs/JobSystem.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class FracturePanelTest : public ::testing::Test
{
protected:
    FracturePanelTest()
    {
        destruction::ImportedScene scene;
        scene.objects.push_back({.name = "Wall",
                                 .mesh = destruction::MakeBoxMesh({-2, 0, -0.15f}, {2, 3, 0.15f}),
                                 .firstMaterialName = ""});
        std::vector<destruction::DestructionMaterial> materials(2);
        materials[0].name = "concrete";
        materials[1].name = "steel";
        m_preview.Add("walls/wall.gltf", std::move(scene), destruction::DefaultCookConfig(), std::move(materials));
        m_tool.active = "walls/wall.gltf";
    }

    editor::testing::ImGuiTestContext m_context;
    FracturePreview m_preview;
    FractureTool m_tool;
    std::vector<AssetEntry> m_sources{{.path = "walls/wall.gltf", .kind = AssetKind::Source}};
};

} // namespace

TEST_F(FracturePanelTest, Draws_WithAndWithoutCook)
{
    JobSystem jobs;
    FractureRequests requests;
    (void)m_context.Frame([&] { requests = DrawFracturePanel(m_tool, m_preview, m_sources); });
    EXPECT_FALSE(requests.saveAndCook || requests.open.has_value());
    ASSERT_EQ(m_preview.Update(jobs), 1u);
    m_tool.object = "Wall";
    (void)m_context.Frame([&] { requests = DrawFracturePanel(m_tool, m_preview, m_sources); });
    EXPECT_EQ(m_preview.Update(jobs), 0u); // drawing changes nothing
}

TEST_F(FracturePanelTest, StoreSettings_ObjectEntryOverridesDefaults)
{
    FracturePreview::Entry& entry = *m_preview.Find("walls/wall.gltf");
    destruction::ObjectCookSettings settings = EditedSettings(entry, "Wall");
    EXPECT_EQ(settings.material, "concrete");
    settings.material = "steel";
    settings.levels[0].count = 3;
    StoreSettings(entry, "Wall", settings);
    EXPECT_EQ(EditedSettings(entry, "Wall").material, "steel");
    EXPECT_EQ(EditedSettings(entry, "").material, "concrete");
    destruction::ObjectCookSettings defaults = EditedSettings(entry, "");
    defaults.levels[0].count = 9;
    StoreSettings(entry, "", defaults);
    EXPECT_EQ(entry.config.defaults.levels[0].count, 9u);
    EXPECT_EQ(EditedSettings(entry, "Wall").levels[0].count, 3u); // the node entry is complete
}
