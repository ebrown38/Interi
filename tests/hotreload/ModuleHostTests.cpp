// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include "modules/TestComponents.h"
#include "modules/TestService.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using interi::CommandBuffers;
using interi::Entity;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::World;
using interi::hotreload::ModuleHost;
using interi::testing::HrCounter;
using interi::testing::HrHealthV1;
using interi::testing::HrHealthV2;
using interi::testing::HrMarked;

namespace
{

const char* const kModuleV1 = INTERI_TEST_MODULE_V1;
const char* const kModuleV2 = INTERI_TEST_MODULE_V2;
const char* const kModuleV3Migrate = INTERI_TEST_MODULE_V3;
const char* const kModuleV3NoMigrator = INTERI_TEST_MODULE_V4;
const char* const kModuleRefuses = INTERI_TEST_MODULE_V5;
const char* const kModuleBadAbi = INTERI_TEST_MODULE_V6;
const char* const kModuleOtherToolchain = INTERI_TEST_MODULE_V7;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

class ModuleHostTest : public ::testing::Test
{
protected:
    interi::hotreload::ModuleHostDesc Desc() const
    {
        interi::hotreload::ModuleHostDesc desc;
        desc.shadowDirectory = m_temp.Path();
        return desc;
    }

    std::vector<Entity> CreateCounters(std::size_t count)
    {
        std::vector<Entity> entities;
        for (std::size_t i = 0; i < count; ++i)
        {
            entities.push_back(world.CreateEntity(HrCounter{}));
        }
        return entities;
    }

    void Run(ModuleHost& host, std::uint32_t times = 1)
    {
        for (std::uint32_t i = 0; i < times; ++i)
        {
            host.RunSystems(jobs, commands);
        }
    }

    interi::testing::ScopedTempDir m_temp;
    JobSystem jobs{Workers(4)};
    CommandBuffers commands{jobs};
    World world;
};

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::ranges::find(names, name) != names.end();
}

} // namespace

TEST_F(ModuleHostTest, Load_RunsSystemsOnHostWorld)
{
    ModuleHost host(world, Desc());
    const std::vector<Entity> entities = CreateCounters(2000); // several chunks, so the parallel systems split
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    EXPECT_TRUE(host.IsLoaded());
    EXPECT_EQ(host.Generation(), 1u);
    Run(host, 3);
    for (const Entity entity : entities)
    {
        EXPECT_EQ(world.Get<HrCounter>(entity)->value, 3);
    }
    Run(host); // Mark sees value 3 after this run's increment (RunAfter) and adds the tag through commands
    for (const Entity entity : entities)
    {
        EXPECT_TRUE(world.Has<HrMarked>(entity));
    }
}

TEST_F(ModuleHostTest, Reload_SystemsReRegistered)
{
    ModuleHost host(world, Desc());
    const std::vector<Entity> entities = CreateCounters(10);
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    Run(host);
    EXPECT_FALSE(Contains(host.SystemNames(), "Extra"));

    const auto reloaded = host.Load(kModuleV2);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_EQ(host.Generation(), 2u);
    EXPECT_EQ(reloaded->systems, 4u);
    EXPECT_TRUE(Contains(host.SystemNames(), "Extra"));
    Run(host);
    for (const Entity entity : entities)
    {
        EXPECT_EQ(world.Get<HrCounter>(entity)->value, 11); // state survived, new code runs
    }

    ASSERT_TRUE(host.Load(kModuleV1).has_value()); // and back
    Run(host);
    EXPECT_EQ(world.Get<HrCounter>(entities[0])->value, 12);
    EXPECT_FALSE(Contains(host.SystemNames(), "Extra"));
}

TEST_F(ModuleHostTest, ComponentLayoutChanged_Migrates)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    std::vector<Entity> entities;
    for (int i = 0; i < 300; ++i)
    {
        entities.push_back(world.CreateEntity(HrHealthV1{.hp = static_cast<float>(i)}, HrCounter{.value = i}));
    }
    Run(host); // v1 regenerates: hp = i + 1

    const auto reloaded = host.Load(kModuleV3Migrate);
    ASSERT_TRUE(reloaded.has_value()) << reloaded.error().message;
    EXPECT_EQ(reloaded->migratedComponents, 300u);
    for (int i = 0; i < 300; ++i)
    {
        const Entity entity = entities[static_cast<std::size_t>(i)];
        EXPECT_FALSE(world.Has<HrHealthV1>(entity));
        const HrHealthV2* health = world.Get<HrHealthV2>(entity);
        ASSERT_NE(health, nullptr);
        EXPECT_EQ(health->hp, static_cast<float>(i + 1));
        EXPECT_EQ(health->maxHp, interi::testing::kMigratedMaxHp);
        EXPECT_EQ(world.Get<HrCounter>(entity)->value, i + 1); // untouched
    }
    Run(host); // v3 regenerates up to maxHp
    EXPECT_EQ(world.Get<HrHealthV2>(entities[5])->hp, 7.0f);
    EXPECT_EQ(world.Get<HrHealthV2>(entities[299])->hp, interi::testing::kMigratedMaxHp);
}

TEST_F(ModuleHostTest, ComponentLayoutChanged_RejectedWithoutMigrator)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    const Entity entity = world.CreateEntity(HrHealthV1{.hp = 5.0f}, HrCounter{});

    const auto reloaded = host.Load(kModuleV3NoMigrator);
    ASSERT_FALSE(reloaded.has_value());
    EXPECT_EQ(reloaded.error().code, interi::ErrorCode::VersionMismatch);
    EXPECT_NE(reloaded.error().message.find("hotreload_test.Health"), std::string::npos);

    // The old module and the world are untouched.
    EXPECT_EQ(host.Generation(), 1u);
    EXPECT_FALSE(world.Has<HrHealthV2>(entity));
    Run(host);
    EXPECT_EQ(world.Get<HrHealthV1>(entity)->hp, 6.0f);
    EXPECT_EQ(world.Get<HrCounter>(entity)->value, 1);
}

TEST_F(ModuleHostTest, LoadFailure_KeepsOldModule)
{
    ModuleHost host(world, Desc());
    const std::vector<Entity> entities = CreateCounters(4);
    ASSERT_TRUE(host.Load(kModuleV1).has_value());

    const auto missing = host.Load(m_temp.Path() / "no_such_module.so");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, interi::ErrorCode::NotFound);

    const auto refused = host.Load(kModuleRefuses);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, interi::ErrorCode::InvalidArgument);

    const auto badAbi = host.Load(kModuleBadAbi);
    ASSERT_FALSE(badAbi.has_value());
    EXPECT_EQ(badAbi.error().code, interi::ErrorCode::VersionMismatch);

    const auto otherToolchain = host.Load(kModuleOtherToolchain);
    ASSERT_FALSE(otherToolchain.has_value());
    EXPECT_EQ(otherToolchain.error().code, interi::ErrorCode::VersionMismatch);
    EXPECT_NE(otherToolchain.error().message.find("toolchain"), std::string::npos);

    EXPECT_TRUE(host.IsLoaded());
    EXPECT_EQ(host.Generation(), 1u);
    Run(host);
    EXPECT_EQ(world.Get<HrCounter>(entities[0])->value, 1);
}

TEST_F(ModuleHostTest, SaveSections_FollowTheModule_ThroughReloadFailureAndUnload)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    const std::string id = host.Saves().Ids().at(0);
    EXPECT_NE(id.find(":Saved"), std::string::npos);
    const interi::SaveGame first = host.Saves().Capture("one");
    ASSERT_NE(first.Find(id), nullptr);
    EXPECT_EQ(first.Find(id)->version, 1u);

    // A reload swaps the section for the new instance's (version 2).
    ASSERT_TRUE(host.Load(kModuleV2).has_value());
    EXPECT_EQ(host.Saves().Ids().size(), 1u);
    EXPECT_EQ(host.Saves().Capture("two").Find(id)->version, 2u);
    // ...and applying the older save hands the section its old version.
    const auto applied = host.Saves().Apply(first);
    ASSERT_TRUE(applied.has_value());
    EXPECT_EQ(applied->restored.size(), 1u);

    // A failed reload leaves the running instance's section alone.
    world.CreateEntity(HrHealthV1{.hp = 5.0f}, HrCounter{});
    ASSERT_FALSE(host.Load(kModuleV3NoMigrator).has_value());
    EXPECT_EQ(host.Saves().Capture("three").Find(id)->version, 2u);
    ASSERT_FALSE(host.Load(kModuleRefuses).has_value());
    EXPECT_EQ(host.Saves().Capture("four").Find(id)->version, 2u);

    host.Unload();
    EXPECT_TRUE(host.Saves().Ids().empty());
}

TEST_F(ModuleHostTest, HostService_EntriesFollowTheModule_ThroughReloadFailureAndUnload)
{
    interi::testing::TestService service;
    auto desc = Desc();
    desc.services["test.service"] = &service;
    ModuleHost host(world, std::move(desc));
    ASSERT_TRUE(host.Load(kModuleV1).has_value());
    EXPECT_EQ(service.Call(), std::vector<int>{1});

    ASSERT_TRUE(host.Load(kModuleV2).has_value()); // the new instance's entry replaces the old one's
    EXPECT_EQ(service.Call(), std::vector<int>{2});

    world.CreateEntity(HrHealthV1{.hp = 5.0f}, HrCounter{});
    ASSERT_FALSE(host.Load(kModuleV3NoMigrator).has_value()); // rolled back: the running instance's entry is back
    EXPECT_EQ(service.Call(), std::vector<int>{2});
    ASSERT_FALSE(host.Load(kModuleRefuses).has_value());
    EXPECT_EQ(service.Call(), std::vector<int>{2});

    host.Unload();
    EXPECT_EQ(service.Count(), 0u);
}

TEST_F(ModuleHostTest, HostService_Unknown_IsNull)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.Load(kModuleV1).has_value()); // the module asks for "test.service" and copes with nothing
}

TEST_F(ModuleHostTest, Unload_ComponentsStayReadable)
{
    const std::vector<Entity> entities = CreateCounters(3);
    {
        ModuleHost host(world, Desc());
        ASSERT_TRUE(host.Load(kModuleV1).has_value());
        Run(host, 2);
        host.Unload();
        EXPECT_FALSE(host.IsLoaded());
        Run(host); // no-op
    }
    EXPECT_EQ(world.Get<HrCounter>(entities[2])->value, 2);
    EXPECT_TRUE(world.DestroyEntity(entities[2]));
}

namespace
{

interi::AssertAction ContinueHandler(const interi::AssertInfo& /*info*/)
{
    return interi::AssertAction::Continue;
}

// Not trivially copyable: a module-mode registry would refuse it.
struct HrNonTrivial
{
    std::string text;
};

} // namespace

TEST_F(ModuleHostTest, Unload_KeepsHostLoggerAndAssertHandler)
{
    interi::Logger logger;
    interi::Logger* const previousLogger = interi::SetGlobalLogger(&logger);
    const interi::ScopedAssertHandler handler(&ContinueHandler);
    {
        ModuleHost host(world, Desc());
        ASSERT_TRUE(host.Load(kModuleV1).has_value());
        host.Unload();
    }

    EXPECT_EQ(interi::GetGlobalLogger(), &logger);
    EXPECT_EQ(interi::GetAssertHandler(), &ContinueHandler);
    // The host's registry is not left in module mode: a non-trivial type still registers.
    const interi::ComponentTypeId id = interi::ComponentId<HrNonTrivial>();
    EXPECT_NE(interi::ComponentInfoOf(id).destroy, nullptr);
    (void)interi::SetGlobalLogger(previousLogger);
}

#if !defined(_WIN32)
TEST_F(ModuleHostTest, Load_ShadowDirectoryWritableByOthers_Refused)
{
    interi::hotreload::ModuleHostDesc desc;
    desc.shadowDirectory = m_temp.Path() / "open";
    std::filesystem::create_directory(desc.shadowDirectory);
    std::filesystem::permissions(desc.shadowDirectory, std::filesystem::perms::all);
    ModuleHost host(world, desc);
    EXPECT_FALSE(host.Load(kModuleV1).has_value());
    EXPECT_FALSE(host.IsLoaded());
    EXPECT_TRUE(std::filesystem::is_empty(desc.shadowDirectory));
}

TEST_F(ModuleHostTest, Load_ShadowDirectoryIsSymlink_Refused)
{
    std::filesystem::create_directory(m_temp.Path() / "real");
    interi::hotreload::ModuleHostDesc desc;
    desc.shadowDirectory = m_temp.Path() / "link";
    std::filesystem::create_directory_symlink(m_temp.Path() / "real", desc.shadowDirectory);
    ModuleHost host(world, desc);
    EXPECT_FALSE(host.Load(kModuleV1).has_value());
    EXPECT_TRUE(std::filesystem::is_empty(m_temp.Path() / "real"));
}
#endif
