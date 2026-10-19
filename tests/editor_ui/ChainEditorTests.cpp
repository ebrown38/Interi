// SPDX-License-Identifier: MIT
#include <interi/editor/ChainEditing.h>
#include <interi/editor_ui/ChainEditor.h>

#include "../chain/ChainTestUtil.h"
#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;
using chain::testing::TestProject;

namespace
{

class Values final : public chain::IValueReader
{
public:
    [[nodiscard]] const values::Value* Read(std::string_view) const override { return nullptr; }
};

struct Fixture
{
    TestProject project;
    UndoStack undo;
    chain::ChainAsset asset;
    ChainEditorState state;
    ChainDebugger debugger;
    Values values;

    Fixture()
    {
        const ChainEditContext context = Context();
        const std::string start = *AddChainNode(undo, asset, context, "trigger.Start", {20.0f, 20.0f});
        const std::string shoot = *AddChainNode(undo, asset, context, "com.test.a:Shoot", {300.0f, 20.0f});
        const std::string add = *AddChainNode(undo, asset, context, "com.test.a:AddScore", {600.0f, 20.0f});
        EXPECT_TRUE(AddChainLink(undo, asset, context, start, "", shoot).has_value());
        EXPECT_TRUE(AddChainLink(undo, asset, context, shoot, "Hit", add, "payload.damage > 5").has_value());
    }

    [[nodiscard]] ChainEditContext Context() const
    {
        return {.registry = project.chains.get(), .values = project.values.get()};
    }

    ChainEditorHost Host(bool withDebugger)
    {
        ChainEditorHost host;
        host.context = Context();
        host.debugger = withDebugger ? &debugger : nullptr;
        host.values = &values;
        host.playing = withDebugger;
        return host;
    }
};

} // namespace

TEST(ChainEditorUi, NodeGeometry_PinsSitOnTheEdges)
{
    chain::ChainNode node;
    node.pos = {10.0f, 20.0f};
    const chain::EventDefinition definition = chain::testing::ShootEvent();
    const ChainRect rect = ChainNodeRect(node, &definition);
    EXPECT_FLOAT_EQ(rect.x, 10.0f);
    EXPECT_FLOAT_EQ(rect.w, kChainNodeWidth);
    EXPECT_GT(rect.h, kChainNodeHeader);
    EXPECT_FLOAT_EQ(ChainOutputPin(rect, 0)[0], rect.x + rect.w);
    EXPECT_GT(ChainOutputPin(rect, 1)[1], ChainOutputPin(rect, 0)[1]);
    EXPECT_FLOAT_EQ(ChainInputPin(rect)[0], rect.x);
    EXPECT_TRUE(rect.Contains(11.0f, 21.0f));
    EXPECT_FALSE(rect.Contains(0.0f, 0.0f));
    EXPECT_GT(ChainNodeRect(node, nullptr).h, kChainNodeHeader); // unknown event: one row
}

TEST(ChainEditorUi, LinkCurve_StartsAndEndsOnThePins_AndPicksNearPoints)
{
    const std::array<float, 2> from = {0.0f, 0.0f};
    const std::array<float, 2> to = {200.0f, 100.0f};
    const auto curve = ChainLinkCurve(from, to, 10);
    ASSERT_EQ(curve.size(), 11u);
    EXPECT_FLOAT_EQ(curve.front()[0], 0.0f);
    EXPECT_FLOAT_EQ(curve.back()[0], 200.0f);
    EXPECT_FLOAT_EQ(curve.back()[1], 100.0f);
    EXPECT_NEAR(ChainDistanceToLink(from, to, curve[5]), 0.0f, 0.5f);
    EXPECT_GT(ChainDistanceToLink(from, to, {100.0f, -200.0f}), 50.0f);
}

TEST(ChainEditorUi, DrawsNodesLinksPaletteAndProblems)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    f.state.selected = {"Shoot1"};
    (void)context.Frame([&] { (void)DrawChainEditor(f.asset, f.undo, f.state, f.Host(false)); });
    ChainEditorResult result;
    const ImDrawData& data = context.Frame([&] { result = DrawChainEditor(f.asset, f.undo, f.state, f.Host(false)); });
    EXPECT_GT(data.TotalVtxCount, 500);
    EXPECT_FALSE(result.edited);
    EXPECT_FALSE(f.state.palette.empty());
    EXPECT_TRUE(f.state.cacheValid);
    EXPECT_FALSE(HasErrors(f.state.diagnostics));
}

TEST(ChainEditorUi, ProblemsAppearInline_AfterTheAssetBreaks)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    // A bad binding written behind the editor's back (a hand-edited file) shows up as an error on the node.
    f.asset.nodes[2].params.push_back({"delta", {.kind = chain::Binding::Kind::Expression, .text = "1.5"}});
    for (int i = 0; i < 2; ++i)
    {
        (void)context.Frame([&] { (void)DrawChainEditor(f.asset, f.undo, f.state, f.Host(false)); });
    }
    EXPECT_TRUE(HasErrors(f.state.diagnostics));
    EXPECT_FALSE(DiagnosticsForNode(f.state.diagnostics, "AddScore1").empty());
}

TEST(ChainEditorUi, DebuggerUi_ShowsTimelineAndWatches)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    chain::TraceRecord record;
    record.kind = chain::TraceRecord::Kind::Fired;
    record.step = 3;
    record.node = "Start1";
    record.output = "Fired";
    (void)f.debugger.ObserveRecords({record}, [](std::uint32_t) { return std::string_view(); });
    f.debugger.ToggleNodeBreakpoint("Shoot1");
    f.debugger.ToggleLinkBreakpoint(1);
    f.debugger.AddWatch("com.test.a:Health");
    ChainEditorHost host = f.Host(true);
    host.playStep = 3;
    host.paused = true;
    ChainEditorResult result;
    int vertices = 0;
    for (int i = 0; i < 3; ++i) // windows show up a frame late
    {
        vertices = context.Frame([&] { result = DrawChainEditor(f.asset, f.undo, f.state, host); }).TotalVtxCount;
    }
    EXPECT_GT(vertices, 500);
    EXPECT_FALSE(result.stepRequested);
    EXPECT_FALSE(result.resumeRequested);
    EXPECT_GT(f.debugger.NodeHeat("Start1", 3), 0.9f);
}

namespace
{

/// Drives the canvas with synthetic mouse and key input (ImGui applies queued events on the next frame).
struct Driver
{
    editor::testing::ImGuiTestContext& context;
    Fixture& f;
    ChainEditorHost host;
    ChainEditorResult last;

    Driver(editor::testing::ImGuiTestContext& c, Fixture& fixture)
        : context(c)
        , f(fixture)
        , host(fixture.Host(false))
    {
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        for (int i = 0; i < 3; ++i)
        {
            Frame();
        }
    }

    void Frame()
    {
        (void)context.Frame([&] { last = DrawChainEditor(f.asset, f.undo, f.state, host); });
    }

    [[nodiscard]] ImVec2 Screen(float x, float y) const
    {
        return {f.state.canvasOrigin[0] + f.state.pan[0] + x * f.state.zoom,
                f.state.canvasOrigin[1] + f.state.pan[1] + y * f.state.zoom};
    }

    void MoveTo(ImVec2 p)
    {
        ImGui::GetIO().AddMousePosEvent(p.x, p.y);
        Frame();
    }
    void Button(bool down)
    {
        ImGui::GetIO().AddMouseButtonEvent(0, down);
        Frame();
    }
    void Drag(ImVec2 from, ImVec2 to)
    {
        MoveTo(from);
        Button(true);
        for (int i = 1; i <= 4; ++i)
        {
            MoveTo(ImVec2(from.x + (to.x - from.x) * static_cast<float>(i) / 4.0f,
                          from.y + (to.y - from.y) * static_cast<float>(i) / 4.0f));
        }
        Button(false);
    }
};

} // namespace

TEST(ChainEditorUi, DraggingANodeHeader_MovesItAsOneUndoStep)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    Driver d(context, f);
    const std::size_t steps = f.undo.Size();
    const auto before = f.asset.nodes[1].pos; // Shoot1 at (300, 20)
    d.Drag(d.Screen(before[0] + 60.0f, before[1] + 10.0f), d.Screen(before[0] + 100.0f, before[1] + 40.0f));
    EXPECT_TRUE(f.state.selected.contains("Shoot1"));
    EXPECT_NEAR(f.asset.nodes[1].pos[0], before[0] + 40.0f, 6.0f);
    EXPECT_NEAR(f.asset.nodes[1].pos[1], before[1] + 30.0f, 6.0f);
    EXPECT_EQ(f.undo.Size(), steps + 1);
    EXPECT_TRUE(f.undo.Undo());
    EXPECT_FLOAT_EQ(f.asset.nodes[1].pos[0], before[0]);
}

TEST(ChainEditorUi, DraggingFromAnOutputPin_LinksTheNodeUnderTheMouse)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    // Start1 -> Shoot1 exists; link Shoot1.Miss -> AddScore1 by dragging from the Miss pin.
    Driver d(context, f);
    const chain::ChainNode shoot = f.asset.nodes[1];
    const chain::EventDefinition definition = chain::testing::ShootEvent();
    const ChainRect rect = ChainNodeRect(shoot, &definition);
    const auto pin = ChainOutputPin(rect, 1); // Miss
    const std::size_t links = f.asset.links.size();
    d.Drag(d.Screen(pin[0], pin[1]), d.Screen(f.asset.nodes[2].pos[0] + 60.0f, f.asset.nodes[2].pos[1] + 30.0f));
    ASSERT_EQ(f.asset.links.size(), links + 1);
    EXPECT_EQ(f.asset.links.back().fromNode, "Shoot1");
    EXPECT_EQ(f.asset.links.back().fromOutput, "Miss");
    EXPECT_EQ(f.asset.links.back().toNode, "AddScore1");
    EXPECT_TRUE(f.state.message.empty());
}

TEST(ChainEditorUi, DroppingALinkOnATrigger_IsRefusedWithAReason)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    Driver d(context, f);
    const chain::EventDefinition definition = chain::testing::ShootEvent();
    const auto pin = ChainOutputPin(ChainNodeRect(f.asset.nodes[1], &definition), 1);
    const std::size_t links = f.asset.links.size();
    d.Drag(d.Screen(pin[0], pin[1]), d.Screen(f.asset.nodes[0].pos[0] + 60.0f, f.asset.nodes[0].pos[1] + 30.0f));
    EXPECT_EQ(f.asset.links.size(), links);
    EXPECT_NE(f.state.message.find("trigger"), std::string::npos);
}

TEST(ChainEditorUi, ClickThenDelete_RemovesTheNode_AndUndoRestoresIt)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    Driver d(context, f);
    const auto at = f.asset.nodes[1].pos; // Shoot1 (AddScore1 is off the visible canvas)
    d.MoveTo(d.Screen(at[0] + 60.0f, at[1] + 10.0f));
    d.Button(true);
    d.Button(false);
    ASSERT_TRUE(f.state.selected.contains("Shoot1"));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true);
    d.Frame();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
    d.Frame();
    EXPECT_EQ(f.asset.nodes.size(), 2u);
    EXPECT_TRUE(f.undo.Undo());
    EXPECT_EQ(f.asset.nodes.size(), 3u);
}

TEST(ChainEditorUi, ClickingALink_SelectsIt_AndWheelZoomsAroundTheCursor)
{
    editor::testing::ImGuiTestContext context;
    Fixture f;
    Driver d(context, f);
    const chain::EventDefinition shoot = chain::testing::ShootEvent();
    const chain::EventDefinition add = chain::testing::ShootEvent();
    const auto from = ChainOutputPin(ChainNodeRect(f.asset.nodes[1], &shoot), 0);
    const auto to = ChainInputPin(ChainNodeRect(f.asset.nodes[2], &add));
    const auto curve = ChainLinkCurve(from, to);
    const auto mid = curve[curve.size() / 2];
    d.MoveTo(d.Screen(mid[0], mid[1]));
    d.Button(true);
    d.Button(false);
    EXPECT_EQ(f.state.selectedLink, 1);

    const ImVec2 cursor = d.Screen(100.0f, 100.0f);
    d.MoveTo(cursor);
    const float zoom = f.state.zoom;
    ImGui::GetIO().AddMouseWheelEvent(0.0f, 2.0f);
    d.Frame();
    EXPECT_GT(f.state.zoom, zoom);
    const ImVec2 after = d.Screen(100.0f, 100.0f);
    EXPECT_NEAR(after.x, cursor.x, 1.0f); // the point under the cursor stays put
    EXPECT_NEAR(after.y, cursor.y, 1.0f);
}
