// SPDX-License-Identifier: MIT
#include <interi/editor/DataEditing.h>
#include <interi/editor/ModuleConflicts.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <algorithm>

namespace interi::editor
{
namespace
{

chain::EventDefinition Action()
{
    chain::EventDefinition definition;
    definition.outputs = {{.name = "Out", .payload = {}}};
    definition.implementation = std::make_shared<const chain::EventImpl>([](chain::EventCall&) {});
    return definition;
}

const ItemConflict* FindConflict(const ConflictReport& report, ItemKind kind, std::string_view id)
{
    const auto it =
        std::ranges::find_if(report.conflicts, [&](const ItemConflict& c) { return c.kind == kind && c.id == id; });
    return it == report.conflicts.end() ? nullptr : &*it;
}

TEST(ModuleGraphView, Layers_DependenciesBelowDependents)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    const DependencyGraphView view = BuildDependencyGraph(graph);
    ASSERT_EQ(view.nodes.size(), 4u);
    const auto layerOf = [&](std::string_view id)
    { return std::ranges::find(view.nodes, id, &GraphNodeView::id)->layer; };
    EXPECT_EQ(layerOf("com.test.ext"), 0);
    EXPECT_EQ(layerOf("com.test.a"), 1);
    EXPECT_EQ(layerOf("com.test.b"), 1);
    EXPECT_EQ(layerOf("com.test.plugin"), 0) << "a Plugin depends on nothing";
    EXPECT_EQ(view.layerCount, 2);
    const auto edge = std::ranges::find_if(view.edges, [](const GraphEdgeView& e)
                                           { return e.from == "com.test.a" && e.to == "com.test.ext"; });
    ASSERT_NE(edge, view.edges.end());
    EXPECT_EQ(edge->kind, GraphEdgeKind::Requires);
    const auto implicit = std::ranges::find(view.nodes, std::string("com.test.ext"), &GraphNodeView::id);
    EXPECT_TRUE(implicit->implicit);
    // Rows in one layer are distinct.
    EXPECT_NE(std::ranges::find(view.nodes, std::string("com.test.a"), &GraphNodeView::id)->row,
              std::ranges::find(view.nodes, std::string("com.test.b"), &GraphNodeView::id)->row);
}

TEST(ModuleConflictsView, DataAndEvents_ShowWinnerAndShadowed)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    auto data = ProjectData::Create(graph);
    ASSERT_TRUE(data.has_value()) << data.error().message;

    sdk::ModuleSet modules(graph, interi::testing::kSdkTestEngineContracts);
    chain::ChainRegistry chains(modules);
    ASSERT_TRUE(chains.Events().Provide("com.test.a", "com.test.a:Fire", Action()).has_value());
    ASSERT_TRUE(chains.Events().Override("com.test.b", "com.test.a:Fire", Action()).has_value());
    ASSERT_TRUE(chains.Events().Override("com.test.plugin", "com.test.a:Fire", Action()).has_value());
    ASSERT_TRUE(chains.Events().Provide("com.test.b", "com.test.b:Reload", Action()).has_value());

    const ConflictReport report = CollectConflicts(graph, &chains, &(*data)->Data(), nullptr);

    const ItemConflict* fire = FindConflict(report, ItemKind::Event, "com.test.a:Fire");
    ASSERT_NE(fire, nullptr);
    EXPECT_EQ(fire->winner, "com.test.plugin");
    EXPECT_TRUE(fire->overridden);
    EXPECT_FALSE(fire->shared);
    ASSERT_EQ(fire->shadowed.size(), 2u);
    EXPECT_EQ(fire->shadowed[0].provider, "com.test.b");
    EXPECT_EQ(fire->shadowed[1].provider, "com.test.a");
    EXPECT_EQ(FindConflict(report, ItemKind::Event, "com.test.b:Reload"), nullptr) << "one provider is no conflict";

    const ItemConflict* gravity = FindConflict(report, ItemKind::Value, "value.Gravity");
    ASSERT_NE(gravity, nullptr);
    EXPECT_TRUE(gravity->shared);
    EXPECT_EQ(gravity->winner, "com.test.b");
    EXPECT_EQ(gravity->shadowed.size(), 2u);

    const ItemConflict* jump = FindConflict(report, ItemKind::Value, "com.test.a:JumpHeight");
    ASSERT_NE(jump, nullptr);
    EXPECT_EQ(jump->winner, "com.test.b");
    EXPECT_NE(FindConflict(report, ItemKind::Archetype, "com.test.a:Soldier"), nullptr);
    EXPECT_NE(FindConflict(report, ItemKind::Attribute, "attr.Health"), nullptr);

    // Sorted by kind, then id.
    EXPECT_TRUE(std::ranges::is_sorted(report.conflicts, [](const ItemConflict& a, const ItemConflict& b)
                                       { return a.kind != b.kind ? a.kind < b.kind : a.id < b.id; }));
}

TEST(ModuleConflictsView, DeclaredOverrides_ReportAppliedAndNot)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules(graph, interi::testing::kSdkTestEngineContracts);
    chain::ChainRegistry chains(modules);
    ASSERT_TRUE(chains.Events().Provide("com.test.a", "com.test.a:Fire", Action()).has_value());
    ASSERT_TRUE(chains.Events().Override("com.test.b", "com.test.a:Fire", Action()).has_value());

    const ConflictReport report = CollectConflicts(graph, &chains, nullptr, nullptr);
    const auto status = [&](std::string_view owner, std::string_view target)
    {
        return std::ranges::find_if(report.overrides, [&](const DeclaredOverrideStatus& s)
                                    { return s.owner == owner && s.target == target; });
    };
    const auto applied = status("com.test.b", "com.test.a:Fire");
    ASSERT_NE(applied, report.overrides.end());
    EXPECT_TRUE(applied->applied);
    EXPECT_FALSE(applied->targetMissing);
    const auto unapplied = status("com.test.plugin", "com.test.a:Fire");
    ASSERT_NE(unapplied, report.overrides.end());
    EXPECT_FALSE(unapplied->applied) << "the Plugin declares it but provides nothing yet";
    EXPECT_EQ(report.overrides.size(), 5u);
}

TEST(ModuleConflictsView, NoRegistries_NoConflicts)
{
    const ConflictReport report = CollectConflicts(interi::testing::ResolveSdkTestProject(), nullptr, nullptr, nullptr);
    EXPECT_TRUE(report.conflicts.empty());
    EXPECT_FALSE(report.overrides.empty());
}

} // namespace
} // namespace interi::editor
