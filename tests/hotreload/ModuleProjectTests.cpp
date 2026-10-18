// SPDX-License-Identifier: MIT
// ModuleHost::LoadProject / Reload over the shared SDK test project (tests/sdk/modules; libraries built from
// modules/SdkTestModule.cpp).
#include <interi/chain/ChainAsset.h>
#include <interi/chain/CoreLibrary.h>
#include <interi/chain/Executor.h>
#include <interi/core/Log.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/Manifest.h>
#include <interi/testing/ScopedTempDir.h>
#include <interi/values/Characters.h>

#include "../sdk/SdkTestModules.h"
#include "modules/TestComponents.h"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

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

TEST_F(ModuleProjectTest, Data_RegisteredBeforeCode_ProjectValuesSurviveReload)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    EXPECT_EQ(host.Data().Values().Get<float>("com.test.a:JumpHeight"), 2.5f); // B's override
    const auto jump = host.Hooks().Find<Fire>("com.test.a:Jump");
    ASSERT_TRUE(jump.IsValid());
    EXPECT_EQ(jump(0), 25); // module code reads the host's data

    ASSERT_TRUE(host.Data().Values().SetProjectValue("com.test.a:JumpHeight", 4.0f).has_value());
    EXPECT_EQ(jump(0), 40);
    ASSERT_TRUE(host.Reload("com.test.ext").has_value()); // reloads everything
    EXPECT_EQ(host.Data().Values().Get<float>("com.test.a:JumpHeight"), 4.0f);
    EXPECT_EQ(host.Hooks().Find<Fire>("com.test.a:Jump")(0), 40);
}

TEST_F(ModuleProjectTest, Reload_RereadsData_MigratesAttributeBlocks)
{
    // A private copy of the manifests, so one can be edited.
    const std::filesystem::path modules = m_temp.Path() / "modules";
    std::vector<interi::sdk::ModuleManifest> copied;
    for (const char* name : {"ext", "a", "b", "plugin"})
    {
        std::filesystem::create_directories(modules / name);
        std::filesystem::copy_file(interi::testing::kSdkTestModulesDir / name / "module.json",
                                   modules / name / "module.json");
        copied.push_back(interi::sdk::LoadManifest(modules / name).value());
    }
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, copied).has_value());
    const auto layout = host.Data().Characters().Layout("com.test.a:Soldier");
    ASSERT_TRUE(layout.has_value()) << layout.error().message;
    const interi::Entity soldier = world.CreateEntity();
    (void)world.Add(soldier, interi::values::MakeAttributeBlock(*layout).value());

    const auto rewrite = [&](std::string_view from, std::string_view to)
    {
        const std::filesystem::path path = modules / "b" / "module.json";
        std::string text = ReadText(path);
        const std::size_t at = text.find(from);
        ASSERT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        WriteText(path, text);
    };

    // Invalid data fails the reload and keeps what was loaded.
    rewrite(R"("default": 2.5, "min": 0, "max": 20)", R"("default": 25, "min": 0, "max": 20)");
    const auto failed = host.Reload("com.test.b");
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(host.Data().Values().Get<float>("com.test.a:JumpHeight"), 2.5f);
    EXPECT_EQ(host.LoadedModules().size(), 4u);

    // Armor becomes a float attribute: the block is migrated by attribute id.
    rewrite(R"("default": 25, "min": 0, "max": 20)", R"("default": 2.5, "min": 0, "max": 20)");
    rewrite(R"({ "id": "Armor", "type": "int", "default": 10)", R"({ "id": "Armor", "type": "float", "default": 10)");
    const auto stats = host.Reload("com.test.b");
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(stats->migratedAttributeBlocks, 1u);
    EXPECT_EQ(
        interi::values::GetAttribute<float>(*world.Get<interi::values::AttributeBlock>(soldier), "com.test.b:Armor"),
        25.0f);
}

TEST_F(ModuleProjectTest, Unload_RemovesEverything)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    host.Unload();
    EXPECT_FALSE(host.IsLoaded());
    EXPECT_TRUE(host.Hooks().Registry().Ids().empty());
    EXPECT_TRUE(host.Data().Values().Ids().empty());
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

TEST_F(ModuleProjectTest, Chains_UseModuleEventsAndRecompileOnReload)
{
    ModuleHost host(world, Desc());
    ASSERT_TRUE(host.LoadProject(kProject, manifests).has_value());
    ASSERT_NE(host.Chains().Events().Resolve("com.test.a:Hit"), nullptr);
    ASSERT_NE(host.Chains().Events().Resolve(interi::chain::kWorldSetValue), nullptr);

    // Tick -> com.test.a:Hit(amount 1) -> JumpHeight = total (b's override caps it at 20).
    auto asset = interi::chain::ParseChain(R"json({"version": 1, "nodes": [
        {"id": "tick", "event": "trigger.Tick"},
        {"id": "hit", "event": "com.test.a:Hit", "params": {"amount": {"const": 1}}},
        {"id": "set", "event": "world.SetValue",
         "params": {"id": {"const": "com.test.a:JumpHeight"}, "value": {"expr": "float(payload.total)"}}}],
        "links": [{"from": "tick", "to": "hit"}, {"from": "hit", "to": "set"}]})json");
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    interi::chain::ChainExecutor executor(host.Chains(), &host.Data().Values());
    const auto chain = executor.AddChain(*asset);
    ASSERT_TRUE(executor.IsActive(chain))
        << (executor.Diagnostics(chain).empty() ? "" : executor.Diagnostics(chain).front().Text());
    executor.Step(world, jobs, commands);
    EXPECT_EQ(*executor.Values().Read("com.test.a:JumpHeight"), interi::values::Value(11.0f)); // 10 * 1 + 1 load

    // The reloaded module registers a new implementation (its second load); the old one's code is gone, and the
    // executor recompiles against the new one.
    ASSERT_TRUE(host.Reload("com.test.a").has_value());
    executor.Step(world, jobs, commands);
    EXPECT_EQ(*executor.Values().Read("com.test.a:JumpHeight"), interi::values::Value(12.0f));

    // Unloading removes the event: the chain no longer compiles and stays inactive.
    host.Unload();
    executor.Step(world, jobs, commands);
    EXPECT_FALSE(executor.IsActive(chain));
}
