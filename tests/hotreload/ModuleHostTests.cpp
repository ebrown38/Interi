// SPDX-License-Identifier: MIT
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include "modules/TestComponents.h"
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

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

class ModuleHostTest : public ::testing::Test
{
protected:
    interi::hotreload::ModuleHostDesc Desc() const { return {.shadowDirectory = m_temp.Path()}; }

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

    EXPECT_TRUE(host.IsLoaded());
    EXPECT_EQ(host.Generation(), 1u);
    Run(host);
    EXPECT_EQ(world.Get<HrCounter>(entities[0])->value, 1);
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
