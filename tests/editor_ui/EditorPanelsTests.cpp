// SPDX-License-Identifier: MIT
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/LevelBridge.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/gameplay/Vehicle.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class EditorPanelsTest : public ::testing::Test
{
protected:
    EditorPanelsTest()
    {
        gameplay::LevelDef level;
        level.staticBoxes.push_back({.transform = Transform(), .halfExtents = {4, 1, 4}});
        gameplay::DestructibleDef wall;
        wall.asset = "generated/wall.idest";
        level.destructibles.push_back(wall);
        level.spawnPoints.push_back({.name = "player", .position = {0, 0, 5}, .yaw = 0.5f});
        ImportLevel(level, m_document.Scene());
    }

    void DrawAll(MenuRequests& requests)
    {
        (void)DrawDockSpace(false);
        requests = DrawMainMenu(m_document);
        HandleShortcuts(m_document, requests);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 40.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f));
        DrawOutliner(m_document);
        ImGui::SetNextWindowPos(ImVec2(400.0f, 40.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 500.0f));
        DrawInspector(m_document);
        m_toolRequests = DrawToolsPanel(m_tools, m_document);
        m_assetRequests = DrawAssetsPanel(m_assets);
    }

    editor::testing::ImGuiTestContext m_context;
    EditorDocument m_document;
    ToolSettings m_tools;
    ToolRequests m_toolRequests;
    std::vector<AssetEntry> m_assets{{.path = "levels/a.json", .kind = AssetKind::Level},
                                     {.path = "walls/wall.idest", .kind = AssetKind::Destructible}};
    AssetRequests m_assetRequests;
};

} // namespace

TEST_F(EditorPanelsTest, EveryPanel_DrawsWithSelection)
{
    MenuRequests requests;
    for (const EditorEntityId id : {EditorEntityId{0}, EditorEntityId{1}, EditorEntityId{2}, EditorEntityId{3}})
    {
        m_document.Select(id);
        const ImDrawData& data = m_context.Frame([&] { DrawAll(requests); });
        EXPECT_GT(data.TotalVtxCount, 0);
    }
    // A vehicle component (bool and float fields) shows too.
    const Entity entity = *m_document.Scene().Find(1);
    (void)m_document.Scene().GetWorld().Add(entity, gameplay::VehicleComponent{.health = 3.0f});
    m_document.Select(1);
    (void)m_context.Frame([&] { DrawAll(requests); });
    EXPECT_FALSE(requests.quit);
}

TEST_F(EditorPanelsTest, OutlinerClick_SelectsRow)
{
    MenuRequests requests;
    (void)m_context.Frame([&] { DrawAll(requests); });
    (void)m_context.Frame([&] { DrawAll(requests); }); // windows settle
    ImGuiIO& io = ImGui::GetIO();
    // The first row sits right under the outliner's title bar.
    const float rowY = 40.0f + ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y + 4.0f;
    io.AddMousePosEvent(40.0f, rowY);
    (void)m_context.Frame([&] { DrawAll(requests); });
    io.AddMouseButtonEvent(0, true);
    (void)m_context.Frame([&] { DrawAll(requests); });
    io.AddMouseButtonEvent(0, false);
    (void)m_context.Frame([&] { DrawAll(requests); });
    EXPECT_EQ(m_document.Selection(), 1u);
}

TEST_F(EditorPanelsTest, MultiSelectionAndChildren_Draw)
{
    const EditorEntityId child = m_document.Scene().CreateObject("child", {}, 1);
    m_document.Select(1);
    m_document.ToggleSelected(child);
    MenuRequests requests;
    const ImDrawData& data = m_context.Frame([&] { DrawAll(requests); });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_EQ(OutlinerRows(m_document.Scene())[1].depth, 1u);
}

TEST_F(EditorPanelsTest, CtrlZ_UndoesLastEdit)
{
    ASSERT_TRUE(EditField(m_document.Scene(), m_document.Undo(), 1, scene::PrimitiveComponent::kComponentName, "size",
                          Vec3(9.0f)));
    MenuRequests requests;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddKeyEvent(ImGuiKey_Z, true);
    (void)m_context.Frame([&] { DrawAll(requests); });
    EXPECT_FALSE(m_document.Undo().CanUndo());

    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiKey_S, true);
    (void)m_context.Frame([&] { DrawAll(requests); });
    EXPECT_TRUE(requests.save);
}

TEST_F(EditorPanelsTest, ToolsAndAssets_DefaultsRequestNothing)
{
    MenuRequests requests;
    (void)m_context.Frame([&] { DrawAll(requests); });
    EXPECT_FALSE(m_toolRequests.addPrimitive || m_toolRequests.addSpawnPoint || m_toolRequests.focusSelection);
    EXPECT_FALSE(m_assetRequests.openLevel.has_value());
    EXPECT_EQ(m_tools.mode, GizmoMode::Translate);
}
