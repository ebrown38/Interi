// SPDX-License-Identifier: MIT
#include <interi/ecs/World.h>
#include <interi/editor_ui/EditorExtensions.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/FileSystem.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

PanelDef Panel(std::string id, int* calls = nullptr)
{
    return {.id = std::move(id),
            .title = "Stats",
            .draw = [calls](const PanelContext&, IEditorDocument&)
            {
                if (calls != nullptr)
                {
                    ++*calls;
                }
            }};
}

OverlayDef Overlay(std::string id)
{
    return {.id = std::move(id), .draw = [](IViewportCanvas&, IEditorDocument&) {}};
}

/// A document with one selected object that records what is asked of it.
class FakeDocument final : public IEditorDocument
{
public:
    [[nodiscard]] World& GetWorld() override { return m_world; }
    [[nodiscard]] std::vector<std::uint64_t> SelectedIds() const override { return {1}; }
    [[nodiscard]] std::uint64_t Selection() const override { return 1; }
    [[nodiscard]] std::string ObjectName(std::uint64_t id) const override
    {
        names.push_back(id);
        return "object";
    }
    [[nodiscard]] Transform ObjectTransform(std::uint64_t) const override
    {
        return {.position = Vec3(1.0f, 2.0f, 3.0f), .rotation = {}, .scale = Vec3(1.0f)};
    }
    [[nodiscard]] std::filesystem::path DataDirectory() const override { return "data"; }
    [[nodiscard]] const interi::IFileSystem& Files() const override { return m_files; }
    [[nodiscard]] interi::JobSystem& Jobs() override { return m_jobs; }
    [[nodiscard]] std::string ScenePath() const override { return "levels/test.scene.json"; }
    void FocusCamera(const Vec3& target, float distance) override
    {
        focusTarget = target;
        focusDistance = distance;
    }
    std::uint64_t AddMeshObject(std::string name, const Transform&, std::string meshAsset) override
    {
        added.push_back(name + "=" + meshAsset);
        return 7;
    }
    mutable std::vector<std::uint64_t> names;
    Vec3 focusTarget;
    float focusDistance = 0.0f;
    std::vector<std::string> added;

private:
    World m_world;
    interi::MemoryFileSystem m_files;
    interi::JobSystem m_jobs{interi::JobSystemDesc{.workerCount = 0}};
};

class FakeCanvas final : public IViewportCanvas
{
public:
    [[nodiscard]] ViewportRect Rect() const override { return {0, 0, 100, 100}; }
    [[nodiscard]] bool Project(const Vec3& world, float& x, float& y) const override
    {
        x = world.x;
        y = world.y;
        return true;
    }
    [[nodiscard]] Ray ScreenRay(float x, float y) const override
    {
        return {.origin = Vec3(x, y, 0.0f), .direction = Vec3(0, 0, 1)};
    }
    void Line(float, float, float, float, std::uint32_t, float) override { ++lines; }
    void WorldLine(const Vec3&, const Vec3&, std::uint32_t, float) override { ++worldLines; }
    void Text(float, float, std::uint32_t, const char*) override { ++texts; }
    int lines = 0;
    int worldLines = 0;
    int texts = 0;
};

} // namespace

TEST(EditorExtensions, Add_ChecksIdsAndFunctions)
{
    EditorExtensions extensions;
    EXPECT_TRUE(extensions.AddPanel("com.a", Panel("com.a:Stats")).has_value());
    EXPECT_FALSE(extensions.AddPanel("com.a", Panel("Stats")).has_value());       // not prefixed
    EXPECT_FALSE(extensions.AddPanel("com.a", Panel("com.b:Stats")).has_value()); // someone else's
    EXPECT_FALSE(extensions.AddPanel("com.a", Panel("com.a:")).has_value());      // empty name
    EXPECT_FALSE(extensions.AddPanel("com.a", Panel("com.a:Stats")).has_value()); // duplicate
    EXPECT_FALSE(extensions.AddPanel("com.a", {.id = "com.a:None", .title = {}, .draw = nullptr}).has_value());
    EXPECT_FALSE(extensions.AddOverlay("com.a", Overlay("com.a:Stats")).has_value()); // ids are unique across kinds
    EXPECT_TRUE(extensions.AddOverlay("com.a", Overlay("com.a:Grid")).has_value());
    EXPECT_EQ(extensions.PanelCount(), 1u);
    EXPECT_EQ(extensions.OverlayCount(), 1u);
}

TEST(EditorExtensions, ExtractAndRestore_FollowTheProvider)
{
    EditorExtensions extensions;
    ASSERT_TRUE(extensions.AddPanel("com.a", Panel("com.a:P")).has_value());
    ASSERT_TRUE(extensions.AddPanel("com.b", Panel("com.b:P")).has_value());
    ASSERT_TRUE(
        extensions.AddRenderFeature("com.a", {.id = "com.a:F", .contribute = [](DebugDraw&, IEditorDocument&) {}})
            .has_value());
    auto token = extensions.Extract("com.a");
    EXPECT_EQ(extensions.PanelCount(), 1u);
    EXPECT_EQ(extensions.RenderFeatureCount(), 0u);
    extensions.Restore(std::move(token));
    EXPECT_EQ(extensions.PanelCount(), 2u);
    EXPECT_EQ(extensions.RenderFeatureCount(), 1u);
    (void)extensions.Extract("com.nobody"); // nothing to take: harmless
    EXPECT_EQ(extensions.PanelCount(), 2u);
}

TEST(EditorExtensions, Tools_ActivateDeactivateAndGoWithTheirModule)
{
    EditorExtensions extensions;
    int activated = 0;
    int deactivated = 0;
    int frames = 0;
    ASSERT_TRUE(
        extensions
            .AddTool("com.a", {.id = "com.a:Paint",
                               .name = "Paint",
                               .activated = [&] { ++activated; },
                               .deactivated = [&] { ++deactivated; },
                               .frame = [&](IViewportCanvas&, const ToolInput&, IEditorDocument&) { ++frames; }})
            .has_value());
    FakeDocument document;
    FakeCanvas canvas;
    EXPECT_FALSE(extensions.RunActiveTool(canvas, {}, document));
    EXPECT_FALSE(extensions.ActivateTool("com.a:Nope"));
    EXPECT_TRUE(extensions.ActivateTool("com.a:Paint"));
    EXPECT_EQ(activated, 1);
    EXPECT_TRUE(extensions.RunActiveTool(canvas, {}, document));
    EXPECT_EQ(frames, 1);
    EXPECT_TRUE(extensions.ActivateTool("com.a:Paint")); // already active: no second activation
    EXPECT_EQ(activated, 1);

    // The module unloads: the tool deactivates before its code goes, and a rollback brings it back active.
    auto token = extensions.Extract("com.a");
    EXPECT_EQ(deactivated, 1);
    EXPECT_EQ(extensions.ActiveTool(), "");
    EXPECT_FALSE(extensions.RunActiveTool(canvas, {}, document));
    extensions.Restore(std::move(token));
    EXPECT_EQ(extensions.ActiveTool(), "com.a:Paint");
    EXPECT_EQ(activated, 2);

    EXPECT_TRUE(extensions.ActivateTool({}));
    EXPECT_EQ(deactivated, 2);
}

TEST(EditorExtensions, RenderFeatures_AddSceneObjectsAndDecals)
{
    EditorExtensions extensions;
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.a", {.id = "com.a:Mesh",
                                                .render = [](RenderFeatureContext& context, RenderFeatureOutput& output,
                                                             IEditorDocument&) -> Result<void>
                                                {
                                                    output.objects->push_back({.transform = Mat4::Identity()});
                                                    output.decals->push_back({.seed = context.generation});
                                                    return {};
                                                }})
                    .has_value());
    EXPECT_EQ(extensions.RenderFeatureCount(), 1u);
    FakeDocument document;
    std::vector<render::RenderObject> objects(2);
    std::vector<render::Decal> decals;
    RenderFeatureContext context;
    RenderFeatureOutput output{.objects = &objects, .decals = &decals};
    int resets = 0;
    const auto reset = [&resets]() -> Result<void>
    {
        ++resets;
        return {};
    };
    ASSERT_TRUE(extensions.GatherRenderObjects(context, output, document, reset).has_value());
    EXPECT_EQ(objects.size(), 3u) << "the output keeps its contents";
    EXPECT_EQ(decals.size(), 1u);
    EXPECT_EQ(resets, 0);
}

TEST(EditorExtensions, RenderFeatures_PoolFull_ClearsRaisesGenerationAndRunsAgain)
{
    EditorExtensions extensions;
    std::vector<std::uint32_t> generations;
    // The first feature fills the pool on generation 0 only; the second just adds an object each run.
    ASSERT_TRUE(extensions
                    .AddRenderFeature(
                        "com.a", {.id = "com.a:Full",
                                  .render = [&generations](RenderFeatureContext& context, RenderFeatureOutput& output,
                                                           IEditorDocument&) -> Result<void>
                                  {
                                      generations.push_back(context.generation);
                                      output.objects->push_back({});
                                      if (context.generation == 0)
                                      {
                                          return MakeError(ErrorCode::OutOfMemory, "pool full");
                                      }
                                      return {};
                                  }})
                    .has_value());
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.b", {.id = "com.b:One",
                                                .render = [](RenderFeatureContext&, RenderFeatureOutput& output,
                                                             IEditorDocument&) -> Result<void>
                                                {
                                                    output.objects->push_back({});
                                                    return {};
                                                }})
                    .has_value());
    FakeDocument document;
    std::vector<render::RenderObject> objects;
    std::vector<render::Decal> decals;
    RenderFeatureContext context;
    RenderFeatureOutput output{.objects = &objects, .decals = &decals};
    int resets = 0;
    ASSERT_TRUE(extensions
                    .GatherRenderObjects(context, output, document,
                                         [&resets]() -> Result<void>
                                         {
                                             ++resets;
                                             return {};
                                         })
                    .has_value());
    EXPECT_EQ(resets, 1);
    EXPECT_EQ(context.generation, 1u);
    EXPECT_EQ(generations, (std::vector<std::uint32_t>{0, 1}));
    EXPECT_EQ(objects.size(), 2u) << "what the failed pass added is dropped";
}

TEST(EditorExtensions, RenderFeatures_OtherErrorSkipsThatFeature)
{
    EditorExtensions extensions;
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.a", {.id = "com.a:Bad",
                                                .render = [](RenderFeatureContext&, RenderFeatureOutput&,
                                                             IEditorDocument&) -> Result<void>
                                                { return MakeError(ErrorCode::InvalidArgument, "nope"); }})
                    .has_value());
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.b", {.id = "com.b:Good",
                                                .render = [](RenderFeatureContext&, RenderFeatureOutput& output,
                                                             IEditorDocument&) -> Result<void>
                                                {
                                                    output.objects->push_back({});
                                                    return {};
                                                }})
                    .has_value());
    FakeDocument document;
    std::vector<render::RenderObject> objects;
    RenderFeatureContext context;
    RenderFeatureOutput output{.objects = &objects, .decals = nullptr};
    ASSERT_TRUE(extensions.GatherRenderObjects(context, output, document, [] { return Result<void>{}; }).has_value());
    EXPECT_EQ(objects.size(), 1u);
}

TEST(EditorExtensions, Updates_RunEveryFrameWithTheDuration)
{
    EditorExtensions extensions;
    float total = 0.0f;
    ASSERT_TRUE(extensions
                    .AddUpdate("com.a", {.id = "com.a:Tick",
                                         .update = [&total](float seconds, IEditorDocument&) { total += seconds; }})
                    .has_value());
    EXPECT_FALSE(extensions.AddUpdate("com.a", {.id = "com.a:Tick", .update = [](float, IEditorDocument&) {}}));
    EXPECT_EQ(extensions.UpdateCount(), 1u);
    FakeDocument document;
    extensions.RunUpdates(0.25f, document);
    extensions.RunUpdates(0.5f, document);
    EXPECT_FLOAT_EQ(total, 0.75f);
    const auto token = extensions.Extract("com.a");
    EXPECT_EQ(extensions.UpdateCount(), 0u);
    extensions.Restore(token);
    EXPECT_EQ(extensions.UpdateCount(), 1u);
}

TEST(EditorExtensions, RenderFeatures_CanAskForTheSceneToBeReplaced)
{
    EditorExtensions extensions;
    bool preview = false;
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.a", {.id = "com.a:Preview",
                                                .replacesScene = [&preview](IEditorDocument&) { return preview; }})
                    .has_value());
    FakeDocument document;
    EXPECT_FALSE(extensions.ScenePreviewActive(document));
    preview = true;
    EXPECT_TRUE(extensions.ScenePreviewActive(document));
}

TEST(EditorExtensions, Tools_CanBeActivatedThroughTheApi)
{
    EditorExtensions extensions;
    IEditorExtensions& api = extensions;
    ASSERT_TRUE(api.AddTool("com.a", {.id = "com.a:Paint",
                                      .name = "Paint",
                                      .activated = {},
                                      .deactivated = {},
                                      .frame = [](IViewportCanvas&, const ToolInput&, IEditorDocument&) {}})
                    .has_value());
    EXPECT_TRUE(api.ActivateTool("com.a:Paint"));
    EXPECT_EQ(extensions.ActiveTool(), "com.a:Paint");
    EXPECT_FALSE(api.ActivateTool("com.a:Missing"));
    EXPECT_TRUE(api.ActivateTool(""));
    EXPECT_TRUE(extensions.ActiveTool().empty());
}

TEST(EditorExtensions, RenderFeatures_ContributeDebugGeometry)
{
    EditorExtensions extensions;
    ASSERT_TRUE(extensions
                    .AddRenderFeature("com.a", {.id = "com.a:Box",
                                                .contribute = [](DebugDraw& draw, IEditorDocument&)
                                                { draw.AddBox(Vec3(0.0f), Vec3(1.0f), 0xffffffffu); }})
                    .has_value());
    FakeDocument document;
    DebugDraw draw;
    extensions.CollectRenderFeatures(draw, document);
    EXPECT_EQ(draw.Lines().size(), 12u);
    FakeCanvas canvas;
    EditorExtensions::DrawDebug(draw, canvas);
    EXPECT_EQ(canvas.worldLines, 12);
    draw.AddLine(Vec3(0.0f), Vec3(1.0f), 1u);
    EXPECT_EQ(draw.Lines().size(), 13u);
    draw.Clear();
    EXPECT_TRUE(draw.Lines().empty());
}

TEST(EditorExtensions, PanelsDrawWhenVisible_MenuTogglesThem)
{
    editor::testing::ImGuiTestContext context;
    EditorExtensions extensions;
    int calls = 0;
    ASSERT_TRUE(extensions.AddPanel("com.a", Panel("com.a:Stats", &calls)).has_value());
    FakeDocument document;
    (void)context.Frame([&] { extensions.DrawPanels(document); });
    (void)context.Frame([&] { extensions.DrawPanels(document); });
    EXPECT_GE(calls, 1);
    const int before = calls;
    extensions.SetPanelVisible("com.a:Stats", false);
    EXPECT_FALSE(extensions.PanelVisible("com.a:Stats"));
    (void)context.Frame([&] { extensions.DrawPanels(document); });
    EXPECT_EQ(calls, before);
    (void)context.Frame(
        [&]
        {
            if (ImGui::BeginMainMenuBar())
            {
                extensions.DrawMenu();
                ImGui::EndMainMenuBar();
            }
        });
}

TEST(EditorExtensions, PanelContext_CarriesTheCurrentImGuiContext)
{
    editor::testing::ImGuiTestContext context;
    const PanelContext panel = CurrentPanelContext();
    EXPECT_EQ(panel.imgui, ImGui::GetCurrentContext());
    EXPECT_NE(panel.allocAlloc, nullptr);
    EXPECT_NE(panel.allocFree, nullptr);
}

TEST(EditorExtensions, Overlays_RunWithTheViewport)
{
    EditorExtensions extensions;
    int calls = 0;
    ASSERT_TRUE(extensions
                    .AddOverlay("com.a", {.id = "com.a:O",
                                          .draw =
                                              [&](IViewportCanvas& canvas, IEditorDocument&)
                                          {
                                              ++calls;
                                              canvas.Text(0, 0, 0xffffffffu, "x");
                                          }})
                    .has_value());
    FakeDocument document;
    FakeCanvas canvas;
    extensions.DrawOverlays(canvas, document);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(canvas.texts, 1);
}

// A real module library (built against the abstract API only) registers through the host service, runs, and is
// taken back when it unloads.
TEST(EditorExtensions, ModuleLibrary_RegistersThroughTheHostService)
{
#ifdef INTERI_EDITOR_EXTENSION_TEST_MODULE
    if (!hotreload::ModuleHost::Supported())
    {
        GTEST_SKIP() << "static engine: modules can't load";
    }
    EditorExtensions extensions;
    World world;
    hotreload::ModuleHostDesc desc;
    desc.services[kEditorExtensionsService] = &extensions;
    {
        hotreload::ModuleHost host(world, std::move(desc));
        const auto loaded = host.Load(INTERI_EDITOR_EXTENSION_TEST_MODULE);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        EXPECT_EQ(extensions.PanelCount(), 1u);
        EXPECT_EQ(extensions.OverlayCount(), 1u);
        EXPECT_EQ(extensions.ToolCount(), 1u);
        EXPECT_EQ(extensions.RenderFeatureCount(), 1u);

        FakeDocument document;
        FakeCanvas canvas;
        editor::testing::ImGuiTestContext imgui;
        (void)imgui.Frame([&] { extensions.DrawPanels(document); });
        (void)imgui.Frame([&] { extensions.DrawPanels(document); });
        ASSERT_FALSE(document.names.empty());
        EXPECT_EQ(document.names.back(), 1u); // the panel saw the editor's ImGui context
        extensions.DrawOverlays(canvas, document);
        EXPECT_EQ(canvas.lines, 1);
        ASSERT_TRUE(extensions.ActivateTool("local.module:Probe"));
        EXPECT_TRUE(extensions.RunActiveTool(canvas, {.mouseX = 3.0f}, document));
        EXPECT_EQ(canvas.worldLines, 1);
        DebugDraw draw;
        extensions.CollectRenderFeatures(draw, document);
        EXPECT_EQ(draw.Lines().size(), 12u);

        host.Unload(); // the entries leave before the library does
        EXPECT_EQ(extensions.PanelCount() + extensions.OverlayCount() + extensions.ToolCount() +
                      extensions.RenderFeatureCount(),
                  0u);
        EXPECT_EQ(extensions.ActiveTool(), "");
    }
#else
    GTEST_SKIP() << "no shared engine";
#endif
}
