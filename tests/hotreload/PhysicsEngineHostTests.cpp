// SPDX-License-Identifier: MIT
// ModuleHost with Physics Engines: the fixtures in tests/hotreload/physics (libraries built from
// modules/PhysicsTestModule.cpp) are two engines with two versions each, an engine that extends one, and modules that
// declare different combinations, one with adapter libraries.
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using interi::ErrorCode;
using interi::World;
using interi::hotreload::ModuleHost;

namespace
{

using Query = int(int);
using Name = std::string(int);
using Ids = std::vector<std::string>;

class PhysicsHostTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!ModuleHost::Supported())
        {
            GTEST_SKIP() << "gameplay modules need the shared engine build (INTERI_BUILD_SHARED)";
        }
        for (const char* name : {"alpha_1", "alpha_15", "beta_1", "beta_2", "alpha_plus", "game", "plain_game",
                                 "beta_only", "plus_game", "thief"})
        {
            auto manifest = interi::sdk::LoadManifest(std::filesystem::path(INTERI_PHYSICS_TEST_MODULES_DIR) / name);
            ASSERT_TRUE(manifest.has_value()) << name << ": " << manifest.error().message;
            manifests.push_back(std::move(*manifest));
        }
    }

    interi::hotreload::ModuleHostDesc Desc() const
    {
        interi::hotreload::ModuleHostDesc desc;
        desc.shadowDirectory = m_temp.Path() / "shadow";
        desc.librarySearchPaths = {INTERI_PHYSICS_TEST_LIBRARY_DIR};
        desc.engineContracts = {"service.PhysicsQueries"};
        return desc;
    }

    static std::string Engine(const ModuleHost& host, const char* module)
    {
        const auto hook = host.Hooks().Find<Name>(std::string(module) + ":Engine");
        return hook.IsValid() ? hook(0) : std::string("<missing>");
    }

    static std::string Adapter(const ModuleHost& host, const char* module)
    {
        const auto hook = host.Hooks().Find<Name>(std::string(module) + ":Adapter");
        return hook.IsValid() ? hook(0) : std::string("<none>");
    }

    static int Queries(const ModuleHost& host, int x)
    {
        const auto hook = host.Hooks().Find<Query>("service.PhysicsQueries");
        return hook.IsValid() ? hook(x) : -1;
    }

    static interi::sdk::PhysicsEngineRef Pin(const char* id) { return {.id = id, .version = {}}; }

    interi::testing::ScopedTempDir m_temp;
    World world;
    std::vector<interi::sdk::ModuleManifest> manifests;
};

} // namespace

TEST_F(PhysicsHostTest, LoadProject_SelectedEngineLoadsFirst_ModulesAreTold)
{
    ModuleHost host(world, Desc());
    auto stats = host.LoadProject(Ids{"com.test.physgame"}, manifests);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(host.LoadedModules(), (Ids{"test.physics.alpha", "com.test.physgame"}));
    EXPECT_EQ(Engine(host, "com.test.physgame"), "test.physics.alpha@1.5.0"); // the highest installed in ^1
    EXPECT_EQ(Queries(host, 1), 2);
    EXPECT_EQ(host.Hooks().Registry().Provenance("service.PhysicsQueries").front().provider, "test.physics.alpha");
}

TEST_F(PhysicsHostTest, LoadProject_LoadsTheAdapterOfTheSelectedEngine)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(Ids{"com.test.physgame"}, manifests).has_value());
    EXPECT_EQ(Adapter(host, "com.test.physgame"), "alpha");

    auto pinned = host.LoadProject(Ids{"com.test.physgame"}, manifests, Pin("test.physics.beta"));
    ASSERT_TRUE(pinned.has_value()) << pinned.error().message;
    EXPECT_EQ(host.LoadedModules(), (Ids{"test.physics.beta", "com.test.physgame"}));
    EXPECT_EQ(Adapter(host, "com.test.physgame"), "beta"); // only the adapter of the selected engine is loaded
    EXPECT_EQ(Engine(host, "com.test.physgame"), "test.physics.beta@2.0.0");
    EXPECT_EQ(Queries(host, 1), 101);
}

TEST_F(PhysicsHostTest, LoadProject_FallsBackToTheEngineEveryModuleSupports)
{
    ModuleHost host(world, Desc());
    auto stats = host.LoadProject(Ids{"com.test.physgame", "com.test.betaonly"}, manifests);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(host.LoadedModules(), (Ids{"test.physics.beta", "com.test.betaonly", "com.test.physgame"}));
    EXPECT_EQ(Adapter(host, "com.test.physgame"), "beta");
}

TEST_F(PhysicsHostTest, LoadProject_ExtendingEngine_LoadsBaseBelowAndOverridesPerItem)
{
    ModuleHost host(world, Desc());
    auto stats = host.LoadProject(Ids{"com.test.physgame", "com.test.plusgame"}, manifests);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(host.LoadedModules(),
              (Ids{"test.physics.alpha", "test.physics.alpha_plus", "com.test.plusgame", "com.test.physgame"}));
    EXPECT_EQ(Engine(host, "com.test.plusgame"), "test.physics.alpha_plus@1.0.0");
    EXPECT_EQ(Queries(host, 3), 8);                         // the extender's 2 * the base's (3 + 1)
    EXPECT_EQ(Adapter(host, "com.test.physgame"), "alpha"); // the game supports the base, so it gets its adapter
}

TEST_F(PhysicsHostTest, LoadProject_NoCommonEngine_RefusedAndPreviousProjectKept)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(Ids{"com.test.physgame"}, manifests).has_value());
    const auto before = host.LoadedModules();

    auto refused = host.LoadProject(Ids{"com.test.plaingame", "com.test.betaonly"}, manifests);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::VersionMismatch);
    EXPECT_EQ(host.LoadedModules(), before);
    EXPECT_EQ(Engine(host, "com.test.physgame"), "test.physics.alpha@1.5.0");
    EXPECT_EQ(Queries(host, 1), 2);
}

TEST_F(PhysicsHostTest, Reload_Engine_ReloadsEveryModuleBuiltAgainstIt)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(Ids{"com.test.physgame", "com.test.plusgame"}, manifests).has_value());
    auto reloaded = host.Reload("test.physics.alpha");
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_EQ(reloaded->reloadedModules, 4U); // alpha, the extender and both games
    EXPECT_EQ(host.LoadedModules(),
              (Ids{"test.physics.alpha", "test.physics.alpha_plus", "com.test.plusgame", "com.test.physgame"}));
    EXPECT_EQ(Adapter(host, "com.test.physgame"), "alpha");
    EXPECT_EQ(Queries(host, 3), 8);

    auto only = host.Reload("com.test.physgame");
    ASSERT_TRUE(only.has_value()) << only.error().message;
    EXPECT_EQ(only->reloadedModules, 1U);
    EXPECT_EQ(Engine(host, "com.test.physgame"), "test.physics.alpha_plus@1.0.0");
}

TEST_F(PhysicsHostTest, PhysicsContract_FromAModule_RefusesTheLoad)
{
    ModuleHost host(world, Desc());
    auto refused = host.LoadProject(Ids{"com.test.thief"}, manifests);
    ASSERT_FALSE(refused.has_value());
    EXPECT_FALSE(host.IsLoaded());
    EXPECT_EQ(Queries(host, 1), -1);
}

TEST_F(PhysicsHostTest, NoDeclaringModules_NoEngineLoaded)
{
    ModuleHost host(world, Desc());
    interi::sdk::ModuleManifest agnostic; // declares no physicsEngines
    agnostic.id = "com.test.agnostic";
    agnostic.name = agnostic.id;
    agnostic.version = interi::sdk::ParseSemVer("1.0.0").value();
    manifests.push_back(agnostic);
    auto stats = host.LoadProject(Ids{"com.test.agnostic"}, manifests);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(host.LoadedModules(), Ids{"com.test.agnostic"});
}
