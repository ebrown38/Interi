// SPDX-License-Identifier: MIT
#include <interi/editor/ChainEditing.h>

#include "ChainTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;
using namespace interi::chain;
using chain::testing::TestProject;

namespace
{

struct Editor
{
    TestProject project;
    UndoStack undo;
    ChainAsset asset;

    [[nodiscard]] ChainEditContext Context() const
    {
        return {.registry = project.chains.get(), .values = project.values.get()};
    }

    std::string Add(std::string_view event, std::array<float, 2> pos = {0.0f, 0.0f})
    {
        auto id = AddChainNode(undo, asset, Context(), event, pos);
        EXPECT_TRUE(id.has_value()) << (id ? "" : id.error().message);
        return id ? *id : std::string();
    }
};

Binding Expr(std::string text)
{
    return {.kind = Binding::Kind::Expression, .text = std::move(text)};
}

Binding Const(std::string text)
{
    return {.kind = Binding::Kind::Constant, .text = std::move(text)};
}

} // namespace

TEST(ChainEditor, AddNode_UniqueIds_UndoRedo)
{
    Editor e;
    EXPECT_EQ(e.Add("trigger.Start"), "Start1");
    EXPECT_EQ(e.Add("com.test.a:Shoot"), "Shoot1");
    EXPECT_EQ(e.Add("com.test.a:Shoot"), "Shoot2");
    ASSERT_EQ(e.asset.nodes.size(), 3u);
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_EQ(e.asset.nodes.size(), 2u);
    EXPECT_TRUE(e.undo.Redo());
    EXPECT_EQ(e.asset.nodes.size(), 3u);
    EXPECT_FALSE(AddChainNode(e.undo, e.asset, e.Context(), "nope.Nothing", {}).has_value());
}

TEST(ChainEditor, AddLink_RecordsUndo)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_EQ(e.asset.links.size(), 1u);
    EXPECT_EQ(e.asset.links[0].toNode, shoot);
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_TRUE(e.asset.links.empty());
    EXPECT_TRUE(e.undo.Redo());
    EXPECT_EQ(e.asset.links.size(), 1u);
}

TEST(ChainEditor, AddLink_Refused_WhenInvalid)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    const std::size_t steps = e.undo.Size();
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "", start).has_value()); // trigger target
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), start, "Nope", shoot).has_value());
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), start, "", "ghost").has_value());
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value()); // duplicate
    EXPECT_FALSE(AddChainLink(e.undo, e.asset, e.Context(), "Start1", "", shoot, "payload.nothing > 1").has_value());
    EXPECT_EQ(e.undo.Size(), steps + 1);
    EXPECT_EQ(e.asset.links.size(), 1u);
}

TEST(ChainEditor, BindTypeMismatch_Rejected)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string add = e.Add("com.test.a:AddScore"); // delta: int
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", add).has_value());
    const std::size_t steps = e.undo.Size();
    // A float does not narrow to an int.
    const auto refused = BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("1.5"));
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("delta"), std::string::npos);
    EXPECT_FALSE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Const("\"text\"")).has_value());
    EXPECT_FALSE(BindChainInput(e.undo, e.asset, e.Context(), add, "nothing", Const("1")).has_value());
    EXPECT_EQ(e.undo.Size(), steps);
    EXPECT_TRUE(e.asset.nodes[1].params.empty());
    // An int does.
    ASSERT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("2 + 3")).has_value());
    EXPECT_EQ(e.asset.nodes[1].params.size(), 1u);
}

TEST(ChainEditor, Bind_IntPromotesToFloat_AndUnbound)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot"); // range: float
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    EXPECT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), shoot, "range", Expr("4")).has_value());
    EXPECT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), shoot, "range", std::nullopt).has_value());
    EXPECT_TRUE(e.asset.nodes[1].params.empty());
}

TEST(ChainEditor, Bind_ChecksUnlinkedNodesToo)
{
    Editor e;
    const std::string add = e.Add("com.test.a:AddScore");
    EXPECT_FALSE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("1.5")).has_value());
    EXPECT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Const("3")).has_value());
}

TEST(ChainEditor, Bind_PayloadFields_TypeFilteredByUpstream)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    const std::string add = e.Add("com.test.a:AddScore");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Hit", add).has_value());
    // Hit's payload.damage is a float: it can't feed an int input, and a missing field is refused.
    EXPECT_FALSE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("payload.damage")).has_value());
    EXPECT_FALSE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("payload.nothing")).has_value());
    EXPECT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), add, "delta", Expr("int(payload.damage)")).has_value());
}

TEST(ChainEditor, Condition_MustBeBool_AndDragsMerge)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    const std::string add = e.Add("com.test.a:AddScore");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Hit", add).has_value());
    EXPECT_FALSE(SetChainLinkCondition(e.undo, e.asset, e.Context(), 1, "payload.damage + 1").has_value());
    EXPECT_FALSE(SetChainLinkCondition(e.undo, e.asset, e.Context(), 1, "payload.damage >").has_value());
    const std::size_t steps = e.undo.Size();
    for (const char* text : {"payload.damage > 1", "payload.damage > 10", "payload.damage > 100"})
    {
        ASSERT_TRUE(SetChainLinkCondition(e.undo, e.asset, e.Context(), 1, text).has_value());
    }
    EXPECT_EQ(e.undo.Size(), steps + 1); // typing merged into one step
    EXPECT_EQ(e.asset.links[1].condition, "payload.damage > 100");
    e.undo.Seal();
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_TRUE(e.asset.links[1].condition.empty());
    EXPECT_TRUE(SetChainLinkCondition(e.undo, e.asset, e.Context(), 1, "").has_value());
}

TEST(ChainEditor, RemoveNodes_DropsLinksAndGroups_Undoable)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    const std::string add = e.Add("com.test.a:AddScore");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Hit", add).has_value());
    ASSERT_TRUE(GroupChainNodes(e.undo, e.asset, {shoot}, "g").has_value());
    ASSERT_TRUE(RemoveChainNodes(e.undo, e.asset, {shoot}).has_value());
    EXPECT_EQ(e.asset.nodes.size(), 2u);
    EXPECT_TRUE(e.asset.links.empty());
    EXPECT_TRUE(e.asset.groups.empty()); // emptied group goes
    EXPECT_FALSE(RemoveChainNodes(e.undo, e.asset, {"ghost"}).has_value());
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_EQ(e.asset.links.size(), 2u);
    EXPECT_EQ(e.asset.groups.size(), 1u);
}

TEST(ChainEditor, Rename_UpdatesLinksAndGroups)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(GroupChainNodes(e.undo, e.asset, {start, shoot}, "g").has_value());
    ASSERT_TRUE(RenameChainNode(e.undo, e.asset, start, "begin").has_value());
    EXPECT_EQ(e.asset.links[0].fromNode, "begin");
    EXPECT_TRUE(std::ranges::contains(e.asset.groups[0].nodes, "begin"));
    EXPECT_FALSE(RenameChainNode(e.undo, e.asset, "begin", "Shoot1").has_value());
    EXPECT_FALSE(RenameChainNode(e.undo, e.asset, "begin", "no good").has_value());
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_EQ(e.asset.links[0].fromNode, "Start1");
}

TEST(ChainEditor, Move_DragMergesIntoOneStep)
{
    Editor e;
    const std::string start = e.Add("trigger.Start", {10.0f, 10.0f});
    const std::size_t steps = e.undo.Size();
    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(MoveChainNodes(e.undo, e.asset, {start}, {2.0f, 1.0f}).has_value());
    }
    EXPECT_EQ(e.undo.Size(), steps + 1);
    EXPECT_FLOAT_EQ(e.asset.nodes[0].pos[0], 20.0f);
    e.undo.Seal();
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_FLOAT_EQ(e.asset.nodes[0].pos[0], 10.0f);
}

TEST(ChainEditor, CommentsAndGroups_EditUndo)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const auto comment = AddChainComment(e.undo, e.asset, {5.0f, 5.0f}, "note");
    ASSERT_TRUE(comment.has_value());
    ChainComment edited = e.asset.comments[0];
    edited.text = "changed";
    ASSERT_TRUE(EditChainComment(e.undo, e.asset, *comment, edited).has_value());
    EXPECT_EQ(e.asset.comments[0].text, "changed");
    ASSERT_TRUE(MoveChainNodes(e.undo, e.asset, {start}, {1.0f, 1.0f}, {*comment}).has_value());
    EXPECT_FLOAT_EQ(e.asset.comments[0].pos[0], 6.0f);
    const auto group = GroupChainNodes(e.undo, e.asset, {start}, "Setup");
    ASSERT_TRUE(group.has_value());
    // Grouping again moves the node: the old group, now empty, goes.
    ASSERT_TRUE(GroupChainNodes(e.undo, e.asset, {start}, "Other").has_value());
    EXPECT_EQ(e.asset.groups.size(), 1u);
    EXPECT_TRUE(UngroupChain(e.undo, e.asset, e.asset.groups[0].id).has_value());
    EXPECT_TRUE(e.asset.groups.empty());
    EXPECT_TRUE(RemoveChainComment(e.undo, e.asset, *comment).has_value());
    EXPECT_FALSE(RemoveChainComment(e.undo, e.asset, *comment).has_value());
    while (e.undo.Undo())
    {
    }
    EXPECT_TRUE(e.asset.nodes.empty());
}

TEST(ChainEditor, CopyPaste_RemapsIdsAndKeepsInternalLinks)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot", {100.0f, 0.0f});
    const std::string add = e.Add("com.test.a:AddScore");
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Hit", add).has_value());
    ASSERT_TRUE(BindChainInput(e.undo, e.asset, e.Context(), shoot, "range", Const("3")).has_value());
    const ChainClipboard clipboard = CopyChainNodes(e.asset, {start, shoot});
    EXPECT_EQ(clipboard.nodes.size(), 2u);
    EXPECT_EQ(clipboard.links.size(), 1u); // Shoot -> AddScore leaves the selection
    auto pasted = PasteChainNodes(e.undo, e.asset, clipboard, {0.0f, 50.0f});
    ASSERT_TRUE(pasted.has_value());
    ASSERT_EQ(pasted->size(), 2u);
    EXPECT_EQ(e.asset.nodes.size(), 5u);
    EXPECT_EQ(e.asset.links.size(), 3u);
    EXPECT_EQ(e.asset.links.back().fromNode, (*pasted)[0]);
    EXPECT_EQ(e.asset.links.back().toNode, (*pasted)[1]);
    EXPECT_FLOAT_EQ(e.asset.nodes[3].pos[1], 50.0f);
    EXPECT_FALSE(e.asset.nodes[4].params.empty());
    EXPECT_TRUE(e.undo.Undo());
    EXPECT_EQ(e.asset.nodes.size(), 3u);
}

TEST(ChainEditor, Validate_MapsDiagnosticsToNodesAndLinks)
{
    Editor e;
    e.asset = chain::testing::Chain(R"json({"version": 1, "nodes": [
        {"id": "s", "event": "trigger.Start"},
        {"id": "a", "event": "com.test.a:AddScore", "params": {"delta": {"expr": "1.5"}}},
        {"id": "lonely", "event": "com.test.a:Echo"}],
        "links": [{"from": "s", "to": "a"}]})json");
    const auto diagnostics = ValidateChain(e.asset, e.Context());
    EXPECT_TRUE(HasErrors(diagnostics));
    EXPECT_FALSE(DiagnosticsForNode(diagnostics, "a").empty());
    EXPECT_TRUE(DiagnosticsForNode(diagnostics, "s").empty());
    const auto lonely = DiagnosticsForNode(diagnostics, "lonely");
    ASSERT_FALSE(lonely.empty());
    EXPECT_EQ(lonely[0]->severity, Diagnostic::Severity::Warning); // unreachable
}

TEST(ChainEditor, Palette_GroupsByModule_AndShowsOverrides)
{
    Editor e;
    EventDefinition better = chain::testing::ShootEvent(3.0f);
    better.category = "Combat";
    ASSERT_TRUE(e.project.chains->Events().Override("com.test.plugin", "com.test.a:Shoot", better).has_value());
    const auto palette = BuildPalette(*e.project.chains);
    ASSERT_FALSE(palette.empty());
    const auto shoot = std::ranges::find(palette, "com.test.a:Shoot", &PaletteEntry::id);
    ASSERT_NE(shoot, palette.end());
    EXPECT_EQ(shoot->module, "com.test.plugin");
    ASSERT_EQ(shoot->shadowed.size(), 1u);
    EXPECT_EQ(shoot->indicator, "provided by com.test.plugin, overrides com.test.a");
    const auto start = std::ranges::find(palette, "trigger.Start", &PaletteEntry::id);
    ASSERT_NE(start, palette.end());
    EXPECT_TRUE(start->indicator.empty());
    EXPECT_EQ(start->kind, EventKind::Trigger);
    EXPECT_TRUE(std::ranges::is_sorted(
        palette, [](const PaletteEntry& a, const PaletteEntry& b)
        { return std::tie(a.module, a.category, a.id) < std::tie(b.module, b.category, b.id); }));
}

TEST(ChainEditor, Palette_Filter_MatchesAllWordsCaseInsensitively)
{
    Editor e;
    const auto palette = BuildPalette(*e.project.chains);
    const auto hits = FilterPalette(palette, "SHOOT test");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0]->id, "com.test.a:Shoot");
    EXPECT_EQ(FilterPalette(palette, "").size(), palette.size());
    EXPECT_TRUE(FilterPalette(palette, "zzzzqqq").empty());
    EXPECT_FALSE(FilterPalette(palette, "world").empty());
}

TEST(ChainEditor, Asset_RoundTripsThroughFile)
{
    Editor e;
    const std::string start = e.Add("trigger.Start", {1.0f, 2.0f});
    const std::string shoot = e.Add("com.test.a:Shoot", {200.0f, 2.0f});
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainComment(e.undo, e.asset, {0.0f, -50.0f}, "hello").has_value());
    ASSERT_TRUE(GroupChainNodes(e.undo, e.asset, {start, shoot}, "All").has_value());
    auto again = ParseChain(WriteChain(e.asset));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, e.asset);
    EXPECT_FALSE(HasErrors(ValidateChain(*again, e.Context())));
}

TEST(ChainEditor, BindingChoices_AreFilteredByType)
{
    Editor e;
    const std::string start = e.Add("trigger.Start");
    const std::string shoot = e.Add("com.test.a:Shoot");
    const std::string add = e.Add("com.test.a:AddScore");
    EXPECT_TRUE(PayloadChoices(e.asset, e.Context(), add).empty()); // nothing linked yet
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), start, "", shoot).has_value());
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Hit", add).has_value());
    const auto payload = PayloadChoices(e.asset, e.Context(), add);
    ASSERT_EQ(payload.size(), 2u); // damage: float, target: entity
    EXPECT_TRUE(FilterChoices(payload, values::ValueType::Int).empty());
    const auto floats = FilterChoices(payload, values::ValueType::Float);
    ASSERT_EQ(floats.size(), 1u);
    EXPECT_EQ(floats[0].name, "damage");
    // A second incoming link that lacks the fields narrows the choices to the common ones.
    ASSERT_TRUE(AddChainLink(e.undo, e.asset, e.Context(), shoot, "Miss", add).has_value());
    EXPECT_TRUE(PayloadChoices(e.asset, e.Context(), add).empty());

    const auto values = ValueChoices(e.project.values.get());
    EXPECT_EQ(FilterChoices(values, values::ValueType::Int).size(), 1u);   // Score
    EXPECT_EQ(FilterChoices(values, values::ValueType::Float).size(), 2u); // Health, and Score promotes
    EXPECT_TRUE(ValueChoices(nullptr).empty());
    EXPECT_TRUE(Assignable(values::ValueType::Float, values::ValueType::U32));
    EXPECT_FALSE(Assignable(values::ValueType::Int, values::ValueType::Float));
}

TEST(ChainEditor, AutoLayout_PlacesNodesLeftToRightAlongLinks)
{
    Editor e;
    e.asset = chain::testing::Chain(R"json({"version": 1, "nodes": [
        {"id": "s", "event": "trigger.Start"}, {"id": "a", "event": "com.test.a:Shoot"},
        {"id": "b", "event": "com.test.a:AddScore"}, {"id": "c", "event": "com.test.a:Echo"}],
        "links": [{"from": "s", "to": "a"}, {"from": "a.Hit", "to": "b"}, {"from": "s", "to": "c"}]})json");
    ASSERT_TRUE(NeedsChainLayout(e.asset));
    AutoLayoutChain(e.asset, e.Context());
    EXPECT_FALSE(NeedsChainLayout(e.asset));
    const auto x = [&](std::string_view id) { return std::ranges::find(e.asset.nodes, id, &ChainNode::id)->pos[0]; };
    const auto y = [&](std::string_view id) { return std::ranges::find(e.asset.nodes, id, &ChainNode::id)->pos[1]; };
    EXPECT_LT(x("s"), x("a"));
    EXPECT_LT(x("a"), x("b"));
    EXPECT_FLOAT_EQ(x("a"), x("c"));
    EXPECT_NE(y("a"), y("c")); // one column, separate rows
    // A cycle doesn't hang it.
    e.asset.links.push_back({.fromNode = "b", .fromOutput = "", .toNode = "a", .condition = ""});
    for (ChainNode& node : e.asset.nodes)
    {
        node.pos = {0.0f, 0.0f};
    }
    AutoLayoutChain(e.asset, e.Context());
    EXPECT_FALSE(NeedsChainLayout(e.asset));
}
