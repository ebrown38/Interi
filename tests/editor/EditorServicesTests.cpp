// SPDX-License-Identifier: MIT
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorServices.h>
#include <interi/editor/Projects.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <fstream>

using namespace interi;
using namespace interi::editor;
using namespace std::chrono_literals;

TEST(Preferences, WriteThenParse_RoundTrips)
{
    Preferences preferences;
    preferences.recentProjects = {"/a", "/b"};
    preferences.autosave = false;
    preferences.autosaveSeconds = 300;
    preferences.keepPlayValues = false;
    preferences.profilerFrameMarks = true;
    preferences.moveSnap = 0.5f;
    auto parsed = ParsePreferences(WritePreferences(preferences));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, preferences);
}

TEST(Preferences, BadOrMissingFields_KeepDefaults)
{
    auto parsed = ParsePreferences(R"({"autosave": "yes", "moveSnap": 2, "extra": 1, "autosaveSeconds": 1})");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->autosave);
    EXPECT_EQ(parsed->moveSnap, 2.0f);
    EXPECT_EQ(parsed->autosaveSeconds, 10u); // clamped
    EXPECT_FALSE(ParsePreferences("[1]").has_value());
}

TEST(Preferences, SaveLoad_AndMissingFileGivesDefaults)
{
    const interi::testing::ScopedTempDir dir;
    const std::filesystem::path path = dir.Path() / "nested" / "editor.json";
    EXPECT_EQ(*LoadPreferences(path), Preferences{});
    Preferences preferences;
    preferences.snap = false;
    ASSERT_TRUE(SavePreferences(path, preferences).has_value());
    EXPECT_EQ(*LoadPreferences(path), preferences);
    EXPECT_FALSE(DefaultPreferencesPath().empty());
}

TEST(Preferences, RecentProjects_MostRecentFirstAndCapped)
{
    Preferences preferences;
    for (int i = 0; i < 12; ++i)
    {
        AddRecentProject(preferences, std::to_string(i));
    }
    AddRecentProject(preferences, "5");
    ASSERT_EQ(preferences.recentProjects.size(), kMaxRecentProjects);
    EXPECT_EQ(preferences.recentProjects[0], "5");
    EXPECT_EQ(preferences.recentProjects[1], "11");
    EXPECT_EQ(std::ranges::count(preferences.recentProjects, "5"), 1);
}

TEST(Autosave, DueOnlyWhenDirtyForTheInterval)
{
    AutosaveTimer timer(60s);
    EXPECT_FALSE(timer.Tick(59s, true));
    EXPECT_TRUE(timer.Tick(1s, true));
    EXPECT_FALSE(timer.Tick(30s, true));
    EXPECT_FALSE(timer.Tick(40s, false)); // a clean document restarts the wait
    EXPECT_FALSE(timer.Tick(40s, true));
    timer.Reset();
    EXPECT_FALSE(timer.Tick(59s, true));
    EXPECT_TRUE(timer.Tick(1s, true));
    EXPECT_EQ(AutosavePath("scenes/a.scene.json"), "scenes/a.scene.json.autosave");
    EXPECT_EQ(AutosavePath(""), "autosave/untitled.scene.json");
}

TEST(Autosave, DocumentWritesWithoutMarkingClean)
{
    MemoryFileSystem files;
    EditorDocument document;
    (void)document.Add([](EditorScene& scene) { return scene.CreateObject("thing"); });
    auto path = document.Autosave(files);
    ASSERT_TRUE(path.has_value()) << path.error().message;
    EXPECT_EQ(*path, "autosave/untitled.scene.json");
    EXPECT_TRUE(files.Exists(*path));
    EXPECT_TRUE(document.IsDirty());
    auto reread = scene::LoadScene(files, *path);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(reread->objects.size(), 1u);
}

TEST(LogBuffer, KeepsTheLastRecords)
{
    LogBuffer buffer(3);
    for (int i = 0; i < 5; ++i)
    {
        const std::string message = std::to_string(i);
        buffer.Add({.level = LogLevel::Info, .threadId = 0, .timestampNs = 0, .category = "Test", .message = message});
    }
    const auto records = buffer.Records();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].message, "2");
    EXPECT_EQ(buffer.TotalWritten(), 5u);
    buffer.Clear();
    EXPECT_TRUE(buffer.Records().empty());
}

TEST(LogBuffer, SinkFedByLoggerWhileReading)
{
    auto buffer = std::make_shared<LogBuffer>(64);
    Logger logger(
        LoggerConfig{.capacity = 256, .startFlushThread = true, .flushInterval = std::chrono::milliseconds(1)});
    logger.AddSink(std::make_unique<LogBufferSink>(buffer));
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back(
            [&logger]
            {
                for (int i = 0; i < 50; ++i)
                {
                    logger.Log(LogLevel::Info, "Test", "message");
                }
            });
    }
    std::size_t seen = 0;
    for (int i = 0; i < 100; ++i)
    {
        seen = std::max(seen, buffer->Records().size()); // reads race with the flush thread's writes
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    logger.Flush();
    EXPECT_LE(buffer->Records().size(), 64u);
    EXPECT_GT(buffer->TotalWritten(), 0u);
    (void)seen;
}

TEST(Projects, CreateEmpty_WritesProjectAndStartupScene)
{
    const interi::testing::ScopedTempDir dir;
    auto project = CreateProject(dir.Path() / "game", "Game");
    ASSERT_TRUE(project.has_value()) << project.error().message;
    EXPECT_EQ(project->startupScene, kDefaultStartupScene);
    auto loaded = sdk::LoadProject(dir.Path() / "game");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->name, "Game");
    EXPECT_TRUE(std::filesystem::exists(dir.Path() / "game" / kDefaultStartupScene));
    EXPECT_FALSE(CreateProject(dir.Path() / "game", "Again").has_value()); // not empty
}

TEST(Projects, FromTemplate_CopiesFilesAndRenames)
{
    const interi::testing::ScopedTempDir dir;
    const std::filesystem::path module = dir.Path() / "modules" / "starter";
    std::filesystem::create_directories(module / "templates" / "coins" / "content");
    std::ofstream(module / "module.json") << R"({"id": "com.test.starter", "version": "1.0.0", "name": "Starter",
        "kind": "Module", "author": "a", "license": "MIT", "copyright": "c", "description": "d", "engine": "*",
        "templates": [{"name": "Coins", "path": "templates/coins", "description": "Collect the coins"}]})";
    std::ofstream(module / "templates" / "coins" / "project.json")
        << R"({"version": 1, "name": "Template", "modules": ["com.test.starter"]})";
    std::ofstream(module / "templates" / "coins" / "content" / "readme.txt") << "hi";

    sdk::Project lookup;
    lookup.directory = dir.Path();
    auto manifests = LoadProjectModules(lookup);
    ASSERT_TRUE(manifests.has_value()) << manifests.error().message;
    const std::vector<ProjectTemplate> templates = ListTemplates(*manifests);
    ASSERT_EQ(templates.size(), 1u);
    EXPECT_EQ(templates[0].module, "com.test.starter");
    EXPECT_EQ(templates[0].description, "Collect the coins");

    auto project = CreateProject(dir.Path() / "mine", "My coins", &templates[0]);
    ASSERT_TRUE(project.has_value()) << project.error().message;
    EXPECT_EQ(project->modules, (std::vector<std::string>{"com.test.starter"}));
    EXPECT_EQ(sdk::LoadProject(dir.Path() / "mine")->name, "My coins");
    EXPECT_TRUE(std::filesystem::exists(dir.Path() / "mine" / "content" / "readme.txt"));
}

namespace
{

sdk::ModuleManifest PhysicsManifest(const char* id, sdk::ModuleKind kind, std::vector<const char*> engines = {})
{
    sdk::ModuleManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.kind = kind;
    manifest.version = sdk::ParseSemVer("1.0.0").value();
    for (std::size_t i = 0; i < engines.size(); ++i)
    {
        manifest.physicsEngines.push_back(
            {.id = engines[i], .version = {}, .isDefault = i == 0 || engines.size() == 1, .library = {}});
    }
    return manifest;
}

} // namespace

TEST(ProjectPhysicsEngine, Describe_ShowsEngineReasonAndPinnableEngines)
{
    const std::vector<sdk::ModuleManifest> manifests = {
        PhysicsManifest("com.t.a", sdk::ModuleKind::Module, {"test.physics.alpha", "test.physics.beta"}),
        PhysicsManifest("com.t.b", sdk::ModuleKind::Module, {"test.physics.beta"}),
        PhysicsManifest("com.t.c", sdk::ModuleKind::Module, {"test.physics.alpha"}),
        PhysicsManifest("test.physics.alpha", sdk::ModuleKind::PhysicsEngine),
        PhysicsManifest("test.physics.beta", sdk::ModuleKind::PhysicsEngine)};
    sdk::Project project;
    project.modules = {"com.t.a", "com.t.b"};
    const sdk::HostInfo host = sdk::CurrentHost();
    auto graph = sdk::ResolveModuleGraph(project.modules, manifests, host, project.physicsEngine);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    const PhysicsEngineStatus status = DescribePhysicsEngine(*graph, project, manifests, host);
    EXPECT_TRUE(status.selected);
    EXPECT_FALSE(status.pinned);
    EXPECT_EQ(status.id, "test.physics.beta");
    EXPECT_EQ(status.version, "1.0.0");
    EXPECT_NE(status.reason.find("fallback of com.t.a"), std::string::npos) << status.reason;
    EXPECT_EQ(status.pinnable, (std::vector<std::string>{"test.physics.beta"})); // B can't use alpha

    project.modules = {"com.t.a"};
    graph = sdk::ResolveModuleGraph(project.modules, manifests, host, project.physicsEngine);
    ASSERT_TRUE(graph.has_value());
    const PhysicsEngineStatus open = DescribePhysicsEngine(*graph, project, manifests, host);
    EXPECT_EQ(open.id, "test.physics.alpha");
    EXPECT_EQ(open.reason, "default of com.t.a");
    EXPECT_EQ(open.pinnable, (std::vector<std::string>{"test.physics.alpha", "test.physics.beta"}));

    project.physicsEngine = sdk::PhysicsEngineRef{.id = "test.physics.beta", .version = {}};
    graph = sdk::ResolveModuleGraph(project.modules, manifests, host, project.physicsEngine);
    ASSERT_TRUE(graph.has_value());
    const PhysicsEngineStatus pinned = DescribePhysicsEngine(*graph, project, manifests, host);
    EXPECT_TRUE(pinned.pinned);
    EXPECT_EQ(pinned.id, "test.physics.beta");
    EXPECT_EQ(pinned.reason, "pinned by the project");
}

TEST(ProjectPhysicsEngine, Describe_NoEngine_NotSelected)
{
    const std::vector<sdk::ModuleManifest> manifests = {PhysicsManifest("com.t.a", sdk::ModuleKind::Module)};
    sdk::Project project;
    project.modules = {"com.t.a"};
    auto graph = sdk::ResolveModuleGraph(project.modules, manifests, sdk::CurrentHost());
    ASSERT_TRUE(graph.has_value());
    const PhysicsEngineStatus status = DescribePhysicsEngine(*graph, project, manifests, sdk::CurrentHost());
    EXPECT_FALSE(status.selected);
    EXPECT_TRUE(status.pinnable.empty());
}

TEST(ProjectPhysicsEngine, AddProjectModule_IncompatibleRefusedProjectUnchanged)
{
    const std::vector<sdk::ModuleManifest> manifests = {
        PhysicsManifest("com.t.a", sdk::ModuleKind::Module, {"test.physics.alpha"}),
        PhysicsManifest("com.t.b", sdk::ModuleKind::Module, {"test.physics.beta"}),
        PhysicsManifest("com.t.c", sdk::ModuleKind::Module),
        PhysicsManifest("test.physics.alpha", sdk::ModuleKind::PhysicsEngine),
        PhysicsManifest("test.physics.beta", sdk::ModuleKind::PhysicsEngine)};
    sdk::Project project;
    project.modules = {"com.t.a"};
    const sdk::Project before = project;
    const sdk::HostInfo host = sdk::CurrentHost();

    auto refused = AddProjectModule(project, manifests, manifests[1], host);
    ASSERT_FALSE(refused.has_value());
    EXPECT_NE(refused.error().message.find("com.t.a"), std::string::npos) << refused.error().message;
    EXPECT_EQ(project, before);

    auto added = AddProjectModule(project, manifests, manifests[2], host);
    ASSERT_TRUE(added.has_value()) << added.error().message;
    EXPECT_EQ(added->modules, (std::vector<std::string>{"com.t.c", "com.t.a"}));
    EXPECT_EQ(project, before);
}

TEST(ProjectPhysicsEngine, PinProjectPhysicsEngine_OnlyWhenEveryModuleSupportsIt)
{
    const std::vector<sdk::ModuleManifest> manifests = {
        PhysicsManifest("com.t.a", sdk::ModuleKind::Module, {"test.physics.alpha"}),
        PhysicsManifest("test.physics.alpha", sdk::ModuleKind::PhysicsEngine),
        PhysicsManifest("test.physics.beta", sdk::ModuleKind::PhysicsEngine)};
    sdk::Project project;
    project.modules = {"com.t.a"};
    const sdk::HostInfo host = sdk::CurrentHost();
    const auto alpha = std::optional(sdk::PhysicsEngineRef{.id = "test.physics.alpha", .version = {}});
    const auto beta = std::optional(sdk::PhysicsEngineRef{.id = "test.physics.beta", .version = {}});

    EXPECT_FALSE(PinProjectPhysicsEngine(project, manifests, beta, host).has_value());
    auto pinned = PinProjectPhysicsEngine(project, manifests, alpha, host);
    ASSERT_TRUE(pinned.has_value()) << pinned.error().message;
    EXPECT_EQ(pinned->physicsEngine->id, "test.physics.alpha");
    EXPECT_FALSE(project.physicsEngine.has_value());
    auto cleared = PinProjectPhysicsEngine(*pinned, manifests, std::nullopt, host);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_FALSE(cleared->physicsEngine.has_value());
}
