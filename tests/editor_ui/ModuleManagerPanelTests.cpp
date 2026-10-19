// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ModuleManagerPanel.h>

#include "../sdk/SdkTestModules.h"
#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class ModuleManagerPanelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        manifests = interi::testing::LoadSdkTestManifests();
        project.name = "t";
        project.modules = {"com.test.b"};
        Refresh();
        state.show = true;
    }

    void Refresh()
    {
        const sdk::HostInfo host = sdk::CurrentHost();
        model = BuildModuleManagerModel(project, manifests, host, trust);
        auto resolved = sdk::ResolveModuleGraph(project.modules, manifests, host);
        ASSERT_TRUE(resolved.has_value());
        graph = BuildDependencyGraph(*resolved);
        conflicts = CollectConflicts(*resolved, nullptr, nullptr, nullptr);
        view.model = &model;
        view.graph = &graph;
        view.conflicts = &conflicts;
        view.plan = [this](const ModuleAction& action) -> Result<ModuleChange>
        {
            const sdk::HostInfo h = sdk::CurrentHost();
            switch (action.kind)
            {
            case ModuleAction::Kind::Enable:
                return EnableModule(project, manifests, action.id, h, trust, action.position);
            case ModuleAction::Kind::Disable:
                return DisableModule(project, manifests, action.id, h, trust);
            case ModuleAction::Kind::Move:
                return MoveModule(project, manifests, action.id, action.position, h, trust);
            }
            return MakeError(ErrorCode::InvalidArgument, "action");
        };
    }

    ModuleManagerRequests Frame()
    {
        ModuleManagerRequests requests;
        (void)context.Frame([&] { requests = DrawModuleManagerPanel(state, view); });
        return requests;
    }

    editor::testing::ImGuiTestContext context;
    std::vector<sdk::ModuleManifest> manifests;
    sdk::Project project;
    TrustStore trust;
    ModuleManagerModel model;
    DependencyGraphView graph;
    ConflictReport conflicts;
    ModuleManagerView view;
    ModuleManagerState state;
};

TEST_F(ModuleManagerPanelTest, EveryTab_DrawsAndRequestsNothing)
{
    for (const ModuleManagerTab tab : {ModuleManagerTab::Modules, ModuleManagerTab::Graph, ModuleManagerTab::Conflicts,
                                       ModuleManagerTab::Plugin, ModuleManagerTab::Docs})
    {
        state.requestedTab = tab;
        (void)Frame();
        const ModuleManagerRequests requests = Frame();
        EXPECT_FALSE(requests.apply.has_value());
        EXPECT_TRUE(requests.trust.empty());
        EXPECT_FALSE(requests.createPlugin.has_value());
        EXPECT_FALSE(requests.runLoop);
    }
}

TEST_F(ModuleManagerPanelTest, EnableUntrustedModule_AsksFirstThenAppliesWithTrust)
{
    state.Queue({.kind = ModuleAction::Kind::Enable, .id = "com.test.a", .position = 0});
    ModuleManagerRequests requests = Frame();
    EXPECT_FALSE(requests.apply.has_value()) << "native code needs a confirmation";
    ASSERT_TRUE(state.pending.has_value());
    EXPECT_FALSE(state.pending->change.needsTrust.empty());

    state.ConfirmPending();
    requests = Frame();
    ASSERT_TRUE(requests.apply.has_value());
    EXPECT_FALSE(state.pending.has_value());
    EXPECT_EQ(requests.apply->project.modules.front(), "com.test.a");
    EXPECT_NE(std::ranges::find(requests.trust, "com.test.a"), requests.trust.end());
}

TEST_F(ModuleManagerPanelTest, Cancel_DropsPendingChange)
{
    state.Queue({.kind = ModuleAction::Kind::Enable, .id = "com.test.a", .position = 0});
    (void)Frame();
    ASSERT_TRUE(state.pending.has_value());
    state.CancelPending();
    const ModuleManagerRequests requests = Frame();
    EXPECT_FALSE(state.pending.has_value());
    EXPECT_FALSE(requests.apply.has_value());
    EXPECT_TRUE(requests.trust.empty());
}

TEST_F(ModuleManagerPanelTest, EnableTrustedModule_AppliesDirectly)
{
    for (const sdk::ModuleManifest& manifest : manifests)
    {
        trust.Trust(manifest);
    }
    Refresh();
    state.Queue({.kind = ModuleAction::Kind::Enable, .id = "com.test.plugin", .position = 0});
    const ModuleManagerRequests requests = Frame();
    ASSERT_TRUE(requests.apply.has_value());
    EXPECT_FALSE(state.pending.has_value());
    EXPECT_TRUE(requests.trust.empty());
}

TEST_F(ModuleManagerPanelTest, DisableRequiredModule_RefusedWithReason)
{
    project.modules = {"com.test.a", "com.test.ext"};
    Refresh();
    state.Queue({.kind = ModuleAction::Kind::Disable, .id = "com.test.ext", .position = 0});
    const ModuleManagerRequests requests = Frame();
    EXPECT_FALSE(requests.apply.has_value());
    EXPECT_NE(state.refusal.find("com.test.a"), std::string::npos);
    EXPECT_FALSE(state.pending.has_value());
}

TEST_F(ModuleManagerPanelTest, MoveTrustedModule_AppliesReorder)
{
    for (const sdk::ModuleManifest& manifest : manifests)
    {
        trust.Trust(manifest);
    }
    project.modules = {"com.test.b", "com.test.a"};
    Refresh();
    state.Queue({.kind = ModuleAction::Kind::Move, .id = "com.test.a", .position = 0});
    const ModuleManagerRequests requests = Frame();
    ASSERT_TRUE(requests.apply.has_value());
    EXPECT_EQ(requests.apply->project.modules, (std::vector<std::string>{"com.test.a", "com.test.b"}));
}

TEST_F(ModuleManagerPanelTest, PluginTargetFromConflictView_ShowsPluginTab)
{
    state.show = false;
    state.SetPluginTarget(PluginTargetKind::Event, "com.test.a:Fire");
    EXPECT_TRUE(state.show);
    EXPECT_EQ(state.pluginTarget, "com.test.a:Fire");
    EXPECT_EQ(state.pluginTargetKind, static_cast<int>(PluginTargetKind::Event));
    (void)Frame();
    EXPECT_FALSE(state.requestedTab.has_value());
}

TEST_F(ModuleManagerPanelTest, NeedsConfirmation_TrustOrEngineChange)
{
    ModuleChange change;
    EXPECT_FALSE(NeedsConfirmation(change));
    change.needsTrust = {"x"};
    EXPECT_TRUE(NeedsConfirmation(change));
    change.needsTrust.clear();
    change.physicsEngineAfter = "engine";
    EXPECT_TRUE(NeedsConfirmation(change));
}

} // namespace
