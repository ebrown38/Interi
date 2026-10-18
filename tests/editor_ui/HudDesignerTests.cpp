// SPDX-License-Identifier: MIT
#include <interi/editor/HudEditing.h>
#include <interi/editor_ui/HudDesigner.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class NoValues final : public chain::IValueReader
{
public:
    [[nodiscard]] const values::Value* Read(std::string_view) const override { return nullptr; }
};

} // namespace

TEST(HudDesigner, DrawsPreviewAndFlagsUnknownValues)
{
    editor::testing::ImGuiTestContext context;
    UndoStack undo;
    hud::HudDocument doc;
    (void)AddHudWidget(undo, doc, hud::WidgetType::Bar);
    (void)AddHudWidget(undo, doc, hud::WidgetType::Button);
    (void)AddHudWidget(undo, doc, hud::WidgetType::Image);
    HudDesignerState state;
    state.selected = "bar1";
    const NoValues values;
    HudDesignerResult result;
    (void)context.Frame([&]
                        { (void)DrawHudDesignerPanel(doc, undo, state, values, {}); }); // windows show up a frame late
    const ImDrawData& data = context.Frame([&] { result = DrawHudDesignerPanel(doc, undo, state, values, {}); });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_FALSE(result.edited);
    ASSERT_EQ(result.unknownValues.size(), 1u);
    EXPECT_EQ(result.unknownValues[0], "value");

    result = {};
    (void)context.Frame([&] { result = DrawHudDesignerPanel(doc, undo, state, values, {"value"}); });
    EXPECT_TRUE(result.unknownValues.empty());
}

TEST(HudDesigner, DrawHudFrame_AddsGeometryForEveryItemKind)
{
    editor::testing::ImGuiTestContext context;
    hud::HudFrame frame;
    frame.items.push_back({.kind = hud::DrawKind::Rect,
                           .rect = {0, 0, 10, 10},
                           .color = {1, 0, 0, 1},
                           .text = {},
                           .image = {},
                           .widget = "r",
                           .centered = false});
    frame.items.push_back({.kind = hud::DrawKind::Image,
                           .rect = {10, 0, 10, 10},
                           .color = {1, 1, 1, 1},
                           .text = {},
                           .image = "a.png",
                           .widget = "i",
                           .centered = false});
    frame.items.push_back({.kind = hud::DrawKind::Text,
                           .rect = {0, 10, 50, 12},
                           .color = {1, 1, 1, 1},
                           .text = "hello",
                           .image = {},
                           .widget = "t",
                           .centered = true});
    const ImDrawData& data = context.Frame(
        [&]
        {
            ImGui::SetNextWindowSize(ImVec2(200, 200));
            ImGui::Begin("host");
            DrawHudFrame(*ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), frame);
            ImGui::End();
        });
    EXPECT_GT(data.TotalVtxCount, 12);
}
