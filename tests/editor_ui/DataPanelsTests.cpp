// SPDX-License-Identifier: MIT
#include <interi/editor/DataEditing.h>
#include <interi/editor_ui/DataPanels.h>

#include "../sdk/SdkTestModules.h"
#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

#include <memory>

using namespace interi;
using namespace interi::editor;

namespace
{

class DataPanelsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Result<std::unique_ptr<ProjectData>> created = ProjectData::Create(interi::testing::ResolveSdkTestProject());
        ASSERT_TRUE(created.has_value()) << created.error().message;
        project = std::move(*created);
    }

    void DrawAll(values::DataRegistry& data)
    {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 400.0f));
        DrawValuesPanel(data, undo, state);
        ImGui::SetNextWindowPos(ImVec2(500.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));
        DrawCharactersPanel(data, undo, state);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 400.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 300.0f));
        DrawDataTablesPanel(data, undo, state);
    }

    editor::testing::ImGuiTestContext context;
    std::unique_ptr<ProjectData> project;
    UndoStack undo;
    DataPanelState state;
};

} // namespace

TEST_F(DataPanelsTest, Panels_DrawProjectData)
{
    values::DataRegistry& data = project->Data();
    ASSERT_TRUE(EditValue(data, undo, "com.test.a:JumpHeight", 3.0f).has_value());
    ASSERT_TRUE(EditAttributeDefault(data, undo, "com.test.a:Soldier", "attr.Health", 175.0f).has_value());
    ASSERT_TRUE(AddTableRow(data, undo, "com.test.a:Guns", "shotgun").has_value());
    data.Tables().LoadRow("com.test.a:Guns", "broken", {{"range", values::Value(500.0f)}}); // shows an issue
    data.Values().LoadProjectValue("value.Gravity", 7.0f);                                  // an unused value

    for (int frame = 0; frame < 3; ++frame)
    {
        const ImDrawData& drawn = context.Frame([&] { DrawAll(data); });
        EXPECT_GT(drawn.TotalVtxCount, 0);
    }
    EXPECT_EQ(state.archetype, "com.test.a:Soldier") << "the first archetype is selected";
    EXPECT_EQ(state.table, "com.test.a:Guns");
    EXPECT_TRUE(state.message.empty());
}

TEST_F(DataPanelsTest, NoData_DrawsHints)
{
    sdk::ModuleSet modules;
    values::DataRegistry empty(modules);
    const ImDrawData& drawn = context.Frame([&] { DrawAll(empty); });
    EXPECT_GT(drawn.TotalVtxCount, 0);
    EXPECT_TRUE(state.table.empty());
}
