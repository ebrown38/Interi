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
