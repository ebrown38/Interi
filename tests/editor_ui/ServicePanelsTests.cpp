// SPDX-License-Identifier: MIT
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ServicePanels.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

TEST(ServicePanels, DrawWithContent_RequestNothing)
{
    editor::testing::ImGuiTestContext context;
    LogBuffer log;
    const std::string message = "hello";
    log.Add({.level = LogLevel::Warning, .threadId = 0, .timestampNs = 1, .category = "Test", .message = message});
    ConsoleState console;
    Preferences preferences;
    ProjectMenuState menu;
    const std::string recent[] = {"/projects/a"};
    const ProjectTemplate templates[] = {{.module = "com.a", .name = "Coins", .description = "d", .directory = {}}};
    ProjectRequests requests;
    bool changed = true;
    ScenePlayRequests play;
    const ImDrawData& data = context.Frame(
        [&]
        {
            DrawConsolePanel(log, console);
            changed = DrawPreferencesPanel(preferences, false, false);
            requests = DrawProjectMenu(menu, recent, templates);
            if (ImGui::Begin("Play"))
            {
                play = DrawPlayControls({.playing = true, .paused = false, .step = 3});
            }
            ImGui::End();
        });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_FALSE(changed);
    EXPECT_FALSE(requests.open.has_value());
    EXPECT_FALSE(requests.createFolder.has_value());
    EXPECT_FALSE(play.play || play.stop || play.pause || play.step);
}

TEST(ServicePanels, F5_PlaysAndStops)
{
    editor::testing::ImGuiTestContext context;
    ImGuiIO& io = ImGui::GetIO();
    ScenePlayRequests play;
    io.AddKeyEvent(ImGuiKey_F5, true);
    (void)context.Frame([&] { play = DrawPlayControls({}); });
    EXPECT_TRUE(play.play);
    io.AddKeyEvent(ImGuiKey_F5, false);
    (void)context.Frame([&] { play = DrawPlayControls({.playing = true, .paused = false, .step = 0}); });
    io.AddKeyEvent(ImGuiKey_F5, true);
    (void)context.Frame([&] { play = DrawPlayControls({.playing = true, .paused = false, .step = 0}); });
    EXPECT_TRUE(play.stop);
}

TEST(ServicePanels, ProjectSettings_ShowsEngineAndRequestsNothingUntilPicked)
{
    editor::testing::ImGuiTestContext context;
    PhysicsEngineStatus status;
    status.selected = true;
    status.id = "test.physics.alpha";
    status.version = "1.5.0";
    status.reason = "default of com.t.a";
    status.pinnable = {"test.physics.alpha", "test.physics.beta"};
    ProjectSettingsRequests requests;
    ProjectMenuState menu;
    menu.refusal = "module 'com.t.b' needs a Physics Engine the project's modules don't support";
    ProjectRequests projectRequests;
    const std::string addable[] = {"com.t.b"};
    const ImDrawData& data = context.Frame(
        [&]
        {
            requests = DrawProjectSettingsPanel(status);
            projectRequests = DrawProjectMenu(menu, {}, {}, addable);
        });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_FALSE(requests.pin.has_value());
    EXPECT_FALSE(requests.clearPin);
    EXPECT_FALSE(projectRequests.addModule.has_value());

    const PhysicsEngineStatus none;
    (void)context.Frame([&] { requests = DrawProjectSettingsPanel(none); });
    EXPECT_FALSE(requests.pin.has_value());
}
