// SPDX-License-Identifier: MIT
// ModuleHost::LoadProject / Reload over the shared SDK test project (tests/sdk/modules; libraries built from
// modules/SdkTestModule.cpp).
#include <interi/core/Log.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>
#include <interi/testing/ScopedTempDir.h>

#include "../sdk/SdkTestModules.h"
#include "modules/TestComponents.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using interi::CommandBuffers;
using interi::ErrorCode;
using interi::JobSystem;
using interi::World;
using interi::hotreload::ModuleHost;
using interi::testing::HrCounter;

namespace
{

using Fire = int(int);
using Greet = std::string(const std::string&);

const std::vector<std::string> kProject = {"com.test.plugin", "com.test.b", "com.test.a"};

/// Collects "unloading module '<id>'" log lines, in order.
class UnloadSink final : public interi::ILogSink
{
public:
    explicit UnloadSink(std::shared_ptr<std::vector<std::string>> unloaded)
        : m_unloaded(std::move(unloaded))
    {
    }

    void Write(const interi::LogRecord& record) override
    {
        constexpr std::string_view kPrefix = "unloading module '";
        if (record.message.starts_with(kPrefix))
        {
            const std::string_view rest = record.message.substr(kPrefix.size());
            m_unloaded->emplace_back(rest.substr(0, rest.find('\'')));
        }
    }

private:
    std::shared_ptr<std::vector<std::string>> m_unloaded;
};

interi::JobSystemDesc Workers(std::uint32_t count)
{
    interi::JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

class ModuleProjectTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!ModuleHost::Supported())
        {
            GTEST_SKIP() << "gameplay modules need the shared engine build (INTERI_BUILD_SHARED)";
        }
    }

    interi::hotreload::ModuleHostDesc Desc() const
    {
        interi::hotreload::ModuleHostDesc desc;
        desc.shadowDirectory = m_temp.Path() / "shadow";
        desc.librarySearchPaths = {INTERI_SDK_TEST_LIBRARY_DIR};
        desc.engineContracts = interi::testing::kSdkTestEngineContracts;
        return desc;
    }

    int CallFire(const ModuleHost& host, int damage) const
    {
        const auto fire = host.Hooks().Find<Fire>("com.test.a:Fire");
        EXPECT_TRUE(fire.IsValid());
        return fire.IsValid() ? fire(damage) : 0;
    }

    interi::testing::ScopedTempDir m_temp;
    JobSystem jobs{Workers(4)};
    CommandBuffers commands{jobs};
    World world;
    std::vector<interi::sdk::ModuleManifest> manifests = interi::testing::LoadSdkTestManifests();
};

} // namespace

TEST_F(ModuleProjectTest, LoadProject_ResolvesHooksAcrossModules)
{
    ModuleHost host(world, Desc());
    auto stats = host.LoadProject(kProject, manifests);
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(host.LoadedModules(),
              (std::vector<std::string>{"com.test.ext", "com.test.a", "com.test.b", "com.test.plugin"}));

    // a: +1, b: x2, plugin: -100, each calling the one below through `next`.
    EXPECT_EQ(CallFire(host, 10), -78);
    EXPECT_EQ(host.Hooks().Find<Greet>("service.Greeter")("bob"), "hello bob!");
    EXPECT_EQ(host.Hooks().Find<Fire>("com.test.b:Reload")(5), 6);
    const auto provenance = host.Hooks().Registry().Provenance("com.test.a:Fire");
    ASSERT_EQ(provenance.size(), 3u);
    EXPECT_EQ(provenance[0].provider, "com.test.plugin");
    EXPECT_EQ(provenance[2].provider, "com.test.a");
}

TEST_F(ModuleProjectTest, LoadProject_RunsEveryModulesSystems)
{
    const interi::Entity entity = world.CreateEntity(HrCounter{});
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    EXPECT_EQ(host.SystemNames(), (std::vector<std::string>{"AddOne", "AddTen"}));
    host.RunSystems(jobs, commands);
    EXPECT_EQ(world.Get<HrCounter>(entity)->value, 11);
}

TEST_F(ModuleProjectTest, LoadProject_Failure_LoadsNothing)
{
    for (auto& manifest : manifests)
    {
        if (manifest.id == "com.test.b")
        {
            manifest.library = "interi_sdk_test_missing";
        }
    }
    ModuleHost host(world, Desc());
    auto stats = host.LoadProject(kProject, manifests);
    ASSERT_FALSE(stats.has_value());
    EXPECT_EQ(stats.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(host.IsLoaded());
    EXPECT_TRUE(host.LoadedModules().empty());
    EXPECT_TRUE(host.Hooks().Registry().Ids().empty());
}

TEST_F(ModuleProjectTest, LoadProject_ResolveError_LoadsNothing)
{
    ModuleHost host(world, Desc());
    const std::vector<std::string> project = {"com.test.b", "com.test.unknown"};
    EXPECT_EQ(host.LoadProject(project, manifests).error().code, ErrorCode::NotFound);
    EXPECT_FALSE(host.IsLoaded());
}

TEST_F(ModuleProjectTest, Reload_ReloadsDependentsAndRestoresState)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    const auto loads = host.Hooks().Find<Fire>("com.test.a:Loads");
    EXPECT_EQ(loads(0), 1);
    const std::uint32_t generation = host.Generation();

    // ext's dependents (a and b) reload with it; a's saved state counts its registrations.
    auto stats = host.Reload("com.test.ext");
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(stats->reloadedModules, 3u);
    EXPECT_EQ(host.Generation(), generation + 1);
    EXPECT_EQ(loads(0), 2);
    EXPECT_EQ(CallFire(host, 10), -78); // the Plugin's override (not reloaded) still wraps the new b and a
    EXPECT_EQ(host.Hooks().Find<Greet>("service.Greeter")("bob"), "hello bob!");
    EXPECT_EQ(host.LoadedModules(),
              (std::vector<std::string>{"com.test.ext", "com.test.a", "com.test.b", "com.test.plugin"}));

    ASSERT_TRUE(host.Reload("com.test.plugin").has_value()); // no dependents
    EXPECT_EQ(loads(0), 2);
    EXPECT_EQ(CallFire(host, 10), -78);
}

TEST_F(ModuleProjectTest, Reload_Failure_KeepsOldModules)
{
    // A private copy of the libraries, so one can be deleted.
    const std::filesystem::path libraries = m_temp.Path() / "libraries";
    std::filesystem::create_directories(libraries);
    for (const auto& entry : std::filesystem::directory_iterator(INTERI_SDK_TEST_LIBRARY_DIR))
    {
        if (entry.path().filename().string().find("interi_sdk_test_") != std::string::npos)
        {
            std::filesystem::copy_file(entry.path(), libraries / entry.path().filename());
        }
    }
    interi::hotreload::ModuleHostDesc desc = Desc();
    desc.librarySearchPaths = {libraries};
    ModuleHost host(world, desc);
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());

    for (const auto& entry : std::filesystem::directory_iterator(libraries))
    {
        if (entry.path().filename().string().find("interi_sdk_test_b") != std::string::npos)
        {
            std::filesystem::remove(entry.path());
        }
    }
    auto stats = host.Reload("com.test.ext"); // b can't be reopened
    ASSERT_FALSE(stats.has_value());
    EXPECT_EQ(CallFire(host, 10), -78);
    EXPECT_EQ(host.Hooks().Find<Fire>("com.test.a:Loads")(0), 1); // the old a is still the one registered
    EXPECT_EQ(host.Hooks().Find<Greet>("service.Greeter")("bob"), "hello bob!");

    EXPECT_EQ(host.Reload("com.test.unknown").error().code, ErrorCode::NotFound);
}

TEST_F(ModuleProjectTest, Unload_RemovesEverything)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    host.Unload();
    EXPECT_FALSE(host.IsLoaded());
    EXPECT_TRUE(host.Hooks().Registry().Ids().empty());
    EXPECT_FALSE(host.Hooks().Find<Fire>("com.test.a:Fire").IsValid());
}

TEST_F(ModuleProjectTest, Reload_UnloadsOldInstancesDependentsFirst)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());

    interi::LoggerConfig config;
    config.startFlushThread = false;
    interi::Logger logger(config);
    auto unloaded = std::make_shared<std::vector<std::string>>();
    logger.AddSink(std::make_unique<UnloadSink>(unloaded));
    interi::Logger* const previous = interi::SetGlobalLogger(&logger);

    ASSERT_TRUE(host.Reload("com.test.ext").has_value());
    logger.Flush();
    EXPECT_EQ(*unloaded, (std::vector<std::string>{"com.test.b", "com.test.a", "com.test.ext"}));

    unloaded->clear();
    host.Unload();
    logger.Flush();
    EXPECT_EQ(*unloaded, (std::vector<std::string>{"com.test.plugin", "com.test.b", "com.test.a", "com.test.ext"}));
    (void)interi::SetGlobalLogger(previous);
}
