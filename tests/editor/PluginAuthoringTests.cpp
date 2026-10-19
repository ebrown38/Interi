// SPDX-License-Identifier: MIT
#include <interi/editor/PluginAuthoring.h>
#include <interi/testing/ScopedTempDir.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace interi::editor
{
namespace
{

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

PluginStubDesc HookStub()
{
    PluginStubDesc desc;
    desc.id = "com.interi.test.hookplugin";
    desc.name = "Hook Plugin";
    desc.author = "Interi";
    desc.target = {.kind = PluginTargetKind::Hook,
                   .id = "com.interi.starter:Example",
                   .ownerVersion = "0.1.0",
                   .hookSignature = "int(int)",
                   .eventInputs = {},
                   .eventOutputs = {}};
    return desc;
}

PluginStubDesc EventStub()
{
    PluginStubDesc desc;
    desc.id = "com.interi.test.eventplugin";
    desc.name = "Event Plugin";
    desc.author = "Interi";
    desc.target = {.kind = PluginTargetKind::Event,
                   .id = "com.interi.starter:LevelComplete",
                   .ownerVersion = "0.1.0",
                   .hookSignature = {},
                   .eventInputs = {},
                   .eventOutputs = {"Out"}};
    return desc;
}

const GeneratedFile& FileNamed(const std::vector<GeneratedFile>& files, std::string_view suffix)
{
    return *std::ranges::find_if(files, [&](const GeneratedFile& f) { return f.path.ends_with(suffix); });
}

TEST(PluginTarget, DescribesHookAndEventFromRegistries)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules(graph, interi::testing::kSdkTestEngineContracts);
    sdk::HookRegistry hooks(modules);
    ASSERT_TRUE(
        hooks
            .Declare<int(int)>("com.test.a", "com.test.a:Fire", [](const sdk::HookNext<int(int)>&, int x) { return x; })
            .has_value());
    chain::ChainRegistry chains(modules);
    chain::EventDefinition event;
    event.outputs = {{.name = "Done", .payload = {}}};
    event.implementation = std::make_shared<const chain::EventImpl>([](chain::EventCall&) {});
    ASSERT_TRUE(chains.Events().Provide("com.test.a", "com.test.a:Hit", std::move(event)).has_value());

    auto hook = DescribePluginTarget(PluginTargetKind::Hook, "com.test.a:Fire", graph, &chains, &hooks);
    ASSERT_TRUE(hook.has_value()) << hook.error().message;
    EXPECT_EQ(hook->hookSignature, "int(int)");
    EXPECT_EQ(hook->ownerVersion, "1.2.0");

    auto hit = DescribePluginTarget(PluginTargetKind::Event, "com.test.a:Hit", graph, &chains, &hooks);
    ASSERT_TRUE(hit.has_value()) << hit.error().message;
    EXPECT_EQ(hit->eventOutputs, std::vector<std::string>{"Done"});

    EXPECT_EQ(DescribePluginTarget(PluginTargetKind::Hook, "com.test.a:Nope", graph, &chains, &hooks).error().code,
              ErrorCode::NotFound);
    EXPECT_EQ(DescribePluginTarget(PluginTargetKind::Event, "com.gone.x:Hit", graph, &chains, &hooks).error().code,
              ErrorCode::NotFound);
}

TEST(PluginStub, LocalHook_DeclaresOverrideAndUsesOverrideMacro)
{
    auto files = GeneratePluginStub(HookStub());
    ASSERT_TRUE(files.has_value()) << files.error().message;
    auto manifest = sdk::ParseManifest(FileNamed(*files, "module.json").text);
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;
    EXPECT_EQ(manifest->kind, sdk::ModuleKind::Plugin);
    ASSERT_EQ(manifest->overrides.size(), 1u);
    EXPECT_EQ(manifest->overrides[0].target, "com.interi.starter:Example");
    EXPECT_EQ(manifest->overrides[0].version.Text(), "^0.1");
    EXPECT_EQ(manifest->library, "com_interi_test_hookplugin");
    const std::string& source = FileNamed(*files, ".cpp").text;
    EXPECT_NE(source.find("INTERI_OVERRIDE(scope, \"com.interi.starter:Example\""), std::string::npos);
    EXPECT_NE(source.find("using Target = int(int);"), std::string::npos);
}

TEST(PluginStub, SharedHook_DeclaresNoOverrideAndProvidesIt)
{
    PluginStubDesc desc = HookStub();
    desc.target.id = "service.Greeter";
    desc.target.ownerVersion.clear();
    desc.target.hookSignature = "void(const std::string&, int)";
    auto files = GeneratePluginStub(desc);
    ASSERT_TRUE(files.has_value()) << files.error().message;
    auto manifest = sdk::ParseManifest(FileNamed(*files, "module.json").text);
    ASSERT_TRUE(manifest.has_value());
    EXPECT_TRUE(manifest->overrides.empty());
    const std::string& source = FileNamed(*files, ".cpp").text;
    EXPECT_NE(source.find("scope.DeclareHook<Target>(\"service.Greeter\", implementation)"), std::string::npos);
    EXPECT_NE(source.find("const std::string& a0, int a1"), std::string::npos);
    EXPECT_NE(source.find("{ next(a0, a1); }"), std::string::npos) << "a void hook returns nothing";
}

TEST(PluginStub, Event_ReplacesImplementationAndOverrides)
{
    auto files = GeneratePluginStub(EventStub());
    ASSERT_TRUE(files.has_value()) << files.error().message;
    const std::string& source = FileNamed(*files, ".cpp").text;
    EXPECT_NE(source.find("definition.implementation = std::make_shared<const EventImpl>"), std::string::npos);
    EXPECT_NE(source.find("events.Override("), std::string::npos);
    EXPECT_NE(source.find("call.Fire(0);"), std::string::npos);
    EXPECT_NE(source.find("Outputs: 0 Out."), std::string::npos);
}

TEST(PluginStub, BadInput_Refused)
{
    PluginStubDesc desc = HookStub();
    desc.id = "Not An Id";
    EXPECT_EQ(GeneratePluginStub(desc).error().code, ErrorCode::InvalidArgument);
    desc = HookStub();
    desc.target.hookSignature = "int";
    EXPECT_EQ(GeneratePluginStub(desc).error().code, ErrorCode::InvalidArgument);
    desc = HookStub();
    desc.name.clear();
    EXPECT_FALSE(GeneratePluginStub(desc).has_value());
}

TEST(PluginStub, Write_RefusesNonEmptyFolder)
{
    interi::testing::ScopedTempDir dir;
    auto written = WritePluginStub(dir.Path() / "p", HookStub());
    ASSERT_TRUE(written.has_value()) << written.error().message;
    EXPECT_EQ(written->size(), 3u);
    EXPECT_TRUE(std::filesystem::exists(dir.Path() / "p" / "module.json"));
    EXPECT_FALSE(WritePluginStub(dir.Path() / "p", HookStub()).has_value());
}

// The committed stubs under tests/editor/plugin_stubs are what the generator writes; CI builds them and loads them
// over the starter (PluginStub_*_LoadsOverStarter). INTERI_UPDATE_GOLDEN=1 rewrites them.
TEST(PluginStub, GeneratorOutput_MatchesCommittedStubs)
{
    const std::filesystem::path root = INTERI_PLUGIN_STUB_DIR;
#if defined(_MSC_VER)
#pragma warning(suppress : 4996) // getenv: read-only, during single-threaded test setup
#endif
    const bool update = std::getenv("INTERI_UPDATE_GOLDEN") != nullptr; // NOLINT(concurrency-mt-unsafe)
    for (const auto& [folder, desc] : {std::pair{"hook", HookStub()}, std::pair{"event", EventStub()}})
    {
        auto files = GeneratePluginStub(desc);
        ASSERT_TRUE(files.has_value()) << files.error().message;
        for (const GeneratedFile& file : *files)
        {
            const std::filesystem::path path = root / folder / file.path;
            if (update)
            {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream(path, std::ios::binary | std::ios::trunc) << file.text;
            }
            EXPECT_EQ(ReadFile(path), file.text) << path.string();
        }
    }
}

TEST(DevLoop, RunsStopBuildReloadReplayInOrder)
{
    std::string order;
    DevLoopHooks hooks;
    hooks.stopPlay = [&] { order += "stop,"; };
    hooks.build = [&]() -> Result<std::string>
    {
        order += "build,";
        return std::string("built\n");
    };
    hooks.reload = [&]() -> Result<std::string>
    {
        order += "reload,";
        return std::string("reloaded\n");
    };
    hooks.replay = [&]() -> Result<void>
    {
        order += "replay";
        return {};
    };
    const DevLoopReport report = RunDevLoop(hooks);
    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.failedAt, DevLoopStage::Done);
    EXPECT_EQ(order, "stop,build,reload,replay");
    EXPECT_EQ(report.output, "built\nreloaded\n");
}

TEST(DevLoop, BuildFailure_StopsBeforeReloadAndKeepsOutput)
{
    bool reloaded = false;
    bool replayed = false;
    DevLoopHooks hooks;
    hooks.build = []() -> Result<std::string> { return MakeError(ErrorCode::Unsupported, "error: no such member"); };
    hooks.reload = [&]() -> Result<std::string>
    {
        reloaded = true;
        return std::string();
    };
    hooks.replay = [&]() -> Result<void>
    {
        replayed = true;
        return {};
    };
    const DevLoopReport report = RunDevLoop(hooks);
    EXPECT_FALSE(report.success);
    EXPECT_EQ(report.failedAt, DevLoopStage::Build);
    EXPECT_NE(report.output.find("no such member"), std::string::npos);
    EXPECT_FALSE(reloaded);
    EXPECT_FALSE(replayed);
}

TEST(DevLoop, ReloadFailure_DoesNotReplay)
{
    bool replayed = false;
    DevLoopHooks hooks;
    hooks.reload = []() -> Result<std::string> { return MakeError(ErrorCode::VersionMismatch, "toolchain"); };
    hooks.replay = [&]() -> Result<void>
    {
        replayed = true;
        return {};
    };
    const DevLoopReport report = RunDevLoop(hooks);
    EXPECT_EQ(report.failedAt, DevLoopStage::Reload);
    EXPECT_EQ(report.message, "toolchain");
    EXPECT_FALSE(replayed);
}

TEST(DevLoop, EmptyHooks_Succeed)
{
    EXPECT_TRUE(RunDevLoop({}).success);
}

TEST(LibraryWatcher, RebuiltLibrary_IsDetected)
{
    interi::testing::ScopedTempDir dir;
    sdk::ModuleManifest manifest;
    manifest.id = "com.test.lib";
    manifest.library = "watched";
    manifest.directory = dir.Path();
    EXPECT_TRUE(FindModuleLibraries(std::span(&manifest, 1), {}).empty());

    for (const char* name : {"libwatched.so", "watched.dll"})
    {
        std::ofstream(dir.Path() / name) << "v1";
    }
    const std::vector<std::filesystem::path> libraries = FindModuleLibraries(std::span(&manifest, 1), {});
    ASSERT_EQ(libraries.size(), 1u);

    LibraryWatcher watcher;
    watcher.Watch(libraries);
    (void)watcher.Poll(); // records the state
    EXPECT_FALSE(watcher.Poll());
    std::ofstream(libraries[0], std::ios::trunc) << "version two, longer";
    EXPECT_TRUE(watcher.Poll());
}

} // namespace
} // namespace interi::editor
