// SPDX-License-Identifier: MIT
#include <interi/editor/ModuleManager.h>
#include <interi/testing/ScopedTempDir.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <algorithm>

namespace interi::editor
{
namespace
{

using interi::testing::LoadSdkTestManifests;

sdk::Project ProjectOf(std::vector<std::string> modules)
{
    sdk::Project project;
    project.name = "t";
    project.modules = std::move(modules);
    return project;
}

bool Has(const std::vector<std::string>& ids, std::string_view id)
{
    return std::ranges::find(ids, id) != ids.end();
}

class ModuleManagerTest : public ::testing::Test
{
protected:
    std::vector<sdk::ModuleManifest> manifests = LoadSdkTestManifests();
    sdk::HostInfo host = sdk::CurrentHost();
    TrustStore trust;
};

TEST_F(ModuleManagerTest, Model_ListsEnabledInOrderThenImplicitThenAvailable)
{
    const ModuleManagerModel model =
        BuildModuleManagerModel(ProjectOf({"com.test.b", "com.test.a"}), manifests, host, trust);
    ASSERT_TRUE(model.graphError.empty()) << model.graphError;
    ASSERT_EQ(model.entries.size(), 4u);
    EXPECT_EQ(model.entries[0].id, "com.test.b");
    EXPECT_EQ(model.entries[0].status, ModuleStatus::Enabled);
    EXPECT_EQ(model.entries[0].position, 0);
    EXPECT_EQ(model.entries[1].id, "com.test.a");
    EXPECT_EQ(model.entries[1].position, 1);
    EXPECT_EQ(model.entries[2].id, "com.test.ext");
    EXPECT_EQ(model.entries[2].status, ModuleStatus::Implicit);
    EXPECT_NE(model.entries[2].reason.find("com.test.a"), std::string::npos);
    EXPECT_EQ(model.entries[3].id, "com.test.plugin");
    EXPECT_EQ(model.entries[3].status, ModuleStatus::Available);
}

TEST_F(ModuleManagerTest, Model_IncompatibleHostIsListedWithReason)
{
    manifests[1].engine = *sdk::ParseVersionRange("^99");
    const ModuleManagerModel model = BuildModuleManagerModel(ProjectOf({}), manifests, host, trust);
    const ModuleEntry* entry = model.Find(manifests[1].id);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ModuleStatus::Incompatible);
    EXPECT_FALSE(entry->reason.empty());
}

TEST_F(ModuleManagerTest, Model_PhysicsEngineIsItsOwnGroup)
{
    sdk::ModuleManifest engine;
    engine.id = "com.test.engine";
    engine.kind = sdk::ModuleKind::PhysicsEngine;
    engine.version = *sdk::ParseSemVer("1.0.0");
    engine.engine = *sdk::ParseVersionRange("*");
    manifests.push_back(engine);
    const ModuleManagerModel model = BuildModuleManagerModel(ProjectOf({}), manifests, host, trust);
    const ModuleEntry* entry = model.Find("com.test.engine");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, ModuleStatus::Engine);
    auto refused = EnableModule(ProjectOf({}), manifests, "com.test.engine", host, trust);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
}

TEST_F(ModuleManagerTest, Enable_PullsInExtensionAndReportsIt)
{
    const sdk::Project project = ProjectOf({});
    auto change = EnableModule(project, manifests, "com.test.a", host, trust);
    ASSERT_TRUE(change.has_value()) << change.error().message;
    EXPECT_TRUE(Has(change->added, "com.test.a"));
    EXPECT_TRUE(Has(change->added, "com.test.ext"));
    EXPECT_TRUE(change->removed.empty());
    EXPECT_EQ(change->project.modules, std::vector<std::string>{"com.test.a"});
    EXPECT_TRUE(project.modules.empty()); // unchanged
}

TEST_F(ModuleManagerTest, Enable_NativeCodeNeedsTrustUntilConfirmed)
{
    auto change = EnableModule(ProjectOf({}), manifests, "com.test.a", host, trust);
    ASSERT_TRUE(change.has_value());
    EXPECT_TRUE(Has(change->needsTrust, "com.test.a"));
    EXPECT_TRUE(Has(change->needsTrust, "com.test.ext")) << "implicit modules are native code too";

    for (const sdk::ModuleManifest& manifest : manifests)
    {
        trust.Trust(manifest);
    }
    change = EnableModule(ProjectOf({}), manifests, "com.test.a", host, trust);
    ASSERT_TRUE(change.has_value());
    EXPECT_TRUE(change->needsTrust.empty());
}

TEST_F(ModuleManagerTest, Enable_UnknownOrAlreadyEnabled_Refused)
{
    EXPECT_EQ(EnableModule(ProjectOf({}), manifests, "com.nope.x", host, trust).error().code, ErrorCode::NotFound);
    EXPECT_EQ(EnableModule(ProjectOf({"com.test.a"}), manifests, "com.test.a", host, trust).error().code,
              ErrorCode::InvalidArgument);
}

TEST_F(ModuleManagerTest, Enable_ConflictRefusedProjectUnchanged)
{
    manifests[1].conflicts.push_back({.id = "com.test.b", .version = *sdk::ParseVersionRange("*")});
    const sdk::Project project = ProjectOf({"com.test.b"});
    auto refused = EnableModule(project, manifests, "com.test.a", host, trust);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(project.modules, std::vector<std::string>{"com.test.b"});
    const ModuleManagerModel model = BuildModuleManagerModel(project, manifests, host, trust);
    ASSERT_NE(model.Find("com.test.a"), nullptr);
    EXPECT_EQ(model.Find("com.test.a")->status, ModuleStatus::Incompatible);
}

TEST_F(ModuleManagerTest, Disable_RequiredExtensionRefusedNamingDependents)
{
    auto refused = DisableModule(ProjectOf({"com.test.a", "com.test.ext"}), manifests, "com.test.ext", host, trust);
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("com.test.a"), std::string::npos);
}

TEST_F(ModuleManagerTest, Disable_DropsModuleAndItsOrphanedDependencies)
{
    auto change = DisableModule(ProjectOf({"com.test.b", "com.test.a"}), manifests, "com.test.b", host, trust);
    ASSERT_TRUE(change.has_value()) << change.error().message;
    EXPECT_EQ(change->removed, std::vector<std::string>{"com.test.b"}); // ext stays: a still needs it
    change = DisableModule(ProjectOf({"com.test.a"}), manifests, "com.test.a", host, trust);
    ASSERT_TRUE(change.has_value());
    EXPECT_TRUE(Has(change->removed, "com.test.ext"));
}

TEST_F(ModuleManagerTest, Disable_NotEnabled_NotFound)
{
    EXPECT_EQ(DisableModule(ProjectOf({}), manifests, "com.test.a", host, trust).error().code, ErrorCode::NotFound);
}

TEST_F(ModuleManagerTest, Move_ReordersAndClamps)
{
    const sdk::Project project = ProjectOf({"com.test.plugin", "com.test.b", "com.test.a"});
    auto change = MoveModule(project, manifests, "com.test.a", 0, host, trust);
    ASSERT_TRUE(change.has_value()) << change.error().message;
    EXPECT_EQ(change->project.modules, (std::vector<std::string>{"com.test.a", "com.test.plugin", "com.test.b"}));
    change = MoveModule(project, manifests, "com.test.plugin", 99, host, trust);
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(change->project.modules.back(), "com.test.plugin");
    EXPECT_TRUE(change->added.empty());
    EXPECT_TRUE(change->removed.empty());
}

TEST_F(ModuleManagerTest, Trust_ChangedAuthorOrLicenseAsksAgain)
{
    trust.Trust(manifests[1]);
    EXPECT_TRUE(trust.IsTrusted(manifests[1]));
    sdk::ModuleManifest changed = manifests[1];
    changed.author = "Someone else";
    EXPECT_FALSE(trust.IsTrusted(changed));
    EXPECT_TRUE(NeedsTrust(changed, trust));
    changed.library.clear();
    EXPECT_FALSE(NeedsTrust(changed, trust)) << "content-only modules run no native code";
}

TEST_F(ModuleManagerTest, Trust_RoundTripsThroughFile)
{
    testing::ScopedTempDir dir;
    const auto path = TrustStorePath(dir.Path() / "editor.json");
    EXPECT_TRUE(TrustStore::Load(path)->Entries().empty()) << "a missing file is an empty store";
    trust.Trust(manifests[2]);
    trust.Trust(manifests[0]);
    ASSERT_TRUE(trust.Save(path).has_value());
    auto loaded = TrustStore::Load(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->Write(), trust.Write());
    EXPECT_TRUE(loaded->IsTrusted(manifests[0]));
    loaded->Revoke(manifests[0].id);
    EXPECT_FALSE(loaded->IsTrusted(manifests[0]));
    EXPECT_FALSE(TrustStore::Parse("{\"trusted\": 3}").has_value());
    EXPECT_FALSE(TrustStore::Parse("nope").has_value());
}

} // namespace
} // namespace interi::editor
