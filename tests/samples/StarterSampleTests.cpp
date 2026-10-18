// SPDX-License-Identifier: MIT
// The starter sample, played headless: a copy of its "Coin Run" template project with the starter module installed,
// opened as a GameSession.
#include <interi/game/GameSession.h>
#include <interi/hud/HudController.h>
#include <interi/hud/MenuStack.h>
#include <interi/platform/InputConfig.h>
#include <interi/runtime/CameraRig.h>
#include <interi/scene/Hierarchy.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace interi;
namespace fs = std::filesystem;

namespace
{

const fs::path kSource = INTERI_STARTER_SOURCE_DIR;
const fs::path kLibraries = INTERI_STARTER_LIBRARY_DIR;

constexpr const char* kCoins = "com.interi.starter:coins";
constexpr const char* kScore = "com.interi.starter:score";
constexpr const char* kWon = "com.interi.starter:won";
constexpr const char* kProgress = "com.interi.starter:progress";

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

class StarterSampleTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_project = m_temp.Path() / "project";
        fs::copy(kSource / "templates" / "coins", m_project, fs::copy_options::recursive);
        fs::create_directories(m_project / "modules" / "com.interi.starter");
        fs::copy_file(kSource / "module.json", m_project / "modules" / "com.interi.starter" / "module.json");
    }

    std::unique_ptr<game::GameSession> Open(std::uint32_t workers = 2, bool withSaves = true)
    {
        game::GameDesc desc;
        desc.projectDirectory = m_project;
        desc.modules.librarySearchPaths = {kLibraries};
        desc.modules.shadowDirectory = m_temp.Path() / "shadow";
        desc.workerCount = workers;
        if (withSaves)
        {
            desc.saveDirectory = m_temp.Path() / "user";
        }
        auto session = game::GameSession::Open(desc);
        EXPECT_TRUE(session.has_value()) << (session ? "" : session.error().message);
        return session ? std::move(*session) : nullptr;
    }

    // Presses `action` and lets the chains settle (the pick-up, the counters and the win take a few steps).
    static void Press(game::GameSession& session, std::string_view action)
    {
        session.RaiseInputAction(action);
        for (int i = 0; i < 4; ++i)
        {
            session.Step();
        }
    }

    static void CollectAll(game::GameSession& session)
    {
        for (const char* action : {"moveRight", "moveRight", "moveRight", "moveDown", "moveLeft"})
        {
            Press(session, action);
        }
    }

    static std::int32_t Int(game::GameSession& session, const char* id)
    {
        const values::Value* value = session.Values().Read(id);
        return value == nullptr ? -1 : std::get<std::int32_t>(value->Variant());
    }
    static bool Bool(game::GameSession& session, const char* id)
    {
        const values::Value* value = session.Values().Read(id);
        return value != nullptr && std::get<bool>(value->Variant());
    }
    static bool ObjectExists(game::GameSession& session, std::uint64_t id)
    {
        return scene::BuildHierarchy(session.GetWorld()).nodes.contains(id);
    }
    static Vec3 PlayerPosition(game::GameSession& session)
    {
        const auto& nodes = scene::BuildHierarchy(session.GetWorld()).nodes;
        return session.GetWorld().Get<Transform>(nodes.at(1).entity)->position;
    }
    static std::int32_t LevelsCompleted(game::GameSession& session)
    {
        const SaveGame game = session.Modules().Saves().Capture("");
        const SaveSection* section = game.Find(kProgress);
        if (section == nullptr)
        {
            return -1;
        }
        auto reader = ArchiveReader::Open(section->data, MakeFourCC("STRT"), 1);
        std::int32_t levels = -1;
        if (reader)
        {
            (*reader)(levels);
        }
        return levels;
    }

    interi::testing::ScopedTempDir m_temp;
    fs::path m_project;
};

} // namespace

TEST_F(StarterSampleTest, Opens_WithTheModuleItsDataAndEveryChain)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(session->Warnings().empty()) << session->Warnings().front();
    EXPECT_EQ(session->Modules().LoadedModules(), std::vector<std::string>{"com.interi.starter"});
    // The project's table rows are read through the module's function.
    EXPECT_EQ(session->Data().Tables().Get<std::int32_t>("com.interi.starter:CoinKinds", "gold", "value").value_or(-1),
              10);
    EXPECT_EQ(Int(*session, kCoins), 0);
    EXPECT_EQ(Int(*session, "com.interi.starter:total"), 5);
    for (const std::uint64_t id : {1u, 10u, 11u, 12u, 13u, 14u})
    {
        EXPECT_TRUE(ObjectExists(*session, id)) << id;
    }
}

TEST_F(StarterSampleTest, WalkingOverCoins_CollectsThemScoresAndWins)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    Press(*session, "moveRight"); // copper at x = 1
    EXPECT_EQ(PlayerPosition(*session), Vec3(1, 0, 0));
    EXPECT_EQ(Int(*session, kCoins), 1);
    EXPECT_EQ(Int(*session, kScore), 1);
    EXPECT_FALSE(ObjectExists(*session, 10));
    EXPECT_TRUE(ObjectExists(*session, 11));
    EXPECT_FALSE(Bool(*session, kWon));
    EXPECT_EQ(LevelsCompleted(*session), 0);

    Press(*session, "moveRight"); // silver: +5
    Press(*session, "moveRight"); // gold: +10
    Press(*session, "moveDown");  // copper
    Press(*session, "moveLeft");  // silver
    EXPECT_EQ(Int(*session, kCoins), 5);
    EXPECT_EQ(Int(*session, kScore), 22);
    EXPECT_TRUE(Bool(*session, kWon));
    EXPECT_EQ(LevelsCompleted(*session), 1); // the module's action ran once
    for (const std::uint64_t id : {10u, 11u, 12u, 13u, 14u})
    {
        EXPECT_FALSE(ObjectExists(*session, id)) << id;
    }
}

TEST_F(StarterSampleTest, WalkingAwayFromCoins_ChangesNothing)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    Press(*session, "moveUp");
    Press(*session, "moveLeft");
    EXPECT_EQ(Int(*session, kCoins), 0);
    EXPECT_EQ(Int(*session, kScore), 0);
}

TEST_F(StarterSampleTest, ResultIsTheSameAtAnyWorkerCount)
{
    std::vector<std::byte> reference;
    for (const std::uint32_t workers : {0u, 1u, 4u, 16u})
    {
        auto session = Open(workers, false);
        ASSERT_NE(session, nullptr);
        CollectAll(*session);
        const std::vector<std::byte> bytes = WriteSave(session->Modules().Saves().Capture("run"));
        if (reference.empty())
        {
            reference = bytes;
        }
        EXPECT_EQ(bytes, reference) << workers << " workers";
    }
}

TEST_F(StarterSampleTest, SaveAndLoad_RestoresTheRunIncludingCollectedCoins)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    Press(*session, "moveRight");
    Press(*session, "moveRight"); // two coins, 6 points, at x = 2
    ASSERT_TRUE(session->SaveToSlot(1, "Two coins").has_value());
    EXPECT_TRUE(session->Slots()[1].occupied);
    EXPECT_EQ(session->Slots()[1].label, "Two coins");

    Press(*session, "moveRight"); // gold
    Press(*session, "moveDown");  // copper
    Press(*session, "moveLeft");  // silver: every coin
    ASSERT_TRUE(Bool(*session, kWon));

    const auto report = session->LoadFromSlot(1);
    ASSERT_TRUE(report.has_value()) << report.error().message;
    EXPECT_TRUE(report->failed.empty());
    EXPECT_EQ(Int(*session, kCoins), 2);
    EXPECT_EQ(Int(*session, kScore), 6);
    EXPECT_FALSE(Bool(*session, kWon));
    EXPECT_EQ(PlayerPosition(*session), Vec3(2, 0, 0));
    EXPECT_FALSE(ObjectExists(*session, 10));
    EXPECT_FALSE(ObjectExists(*session, 11));
    EXPECT_TRUE(ObjectExists(*session, 12)); // still to collect
    EXPECT_EQ(LevelsCompleted(*session), 0); // the module's section came back too

    // The run continues from there.
    Press(*session, "moveRight");
    Press(*session, "moveDown");
    Press(*session, "moveLeft");
    EXPECT_EQ(Int(*session, kCoins), 5);
    EXPECT_TRUE(Bool(*session, kWon));
}

TEST_F(StarterSampleTest, LoadFromEmptyOrCorruptSlot_ChangesNothing)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    Press(*session, "moveRight");
    EXPECT_EQ(session->LoadFromSlot(2).error().code, ErrorCode::NotFound);
    const std::vector<std::byte> junk(40, std::byte{9});
    ASSERT_TRUE(NativeFileSystem(m_temp.Path() / "user").WriteFile("saves/slot0.isav", junk).has_value());
    EXPECT_FALSE(session->LoadFromSlot(0).has_value());
    EXPECT_TRUE(session->Slots()[0].corrupt);
    EXPECT_EQ(Int(*session, kCoins), 1); // the run is intact
}

TEST_F(StarterSampleTest, Saves_AreUnavailableWithoutADirectory)
{
    auto session = Open(2, false);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->SaveToSlot(0, "x").error().code, ErrorCode::Unsupported);
    EXPECT_TRUE(session->Slots().empty());
}

TEST_F(StarterSampleTest, Restart_StartsTheLevelAgain)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    CollectAll(*session);
    ASSERT_TRUE(session->Restart().has_value());
    EXPECT_EQ(Int(*session, kCoins), 0);
    EXPECT_TRUE(ObjectExists(*session, 10));
    EXPECT_EQ(PlayerPosition(*session), Vec3(0, 0, 0));
}

TEST_F(StarterSampleTest, Hud_ShowsTheRun)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    Press(*session, "moveRight");
    const auto hud = hud::LoadHud(session->Files(), "content/ui/hud.hud.json");
    ASSERT_TRUE(hud.has_value()) << hud.error().message;
    EXPECT_TRUE(hud->warnings.empty());
    // Every Value the HUD reads exists in the project.
    const std::vector<std::string> known = session->Data().Values().Ids();
    for (const std::string& id : hud::BoundValues(hud->document))
    {
        EXPECT_NE(std::ranges::find(known, id), known.end()) << id;
    }
    const hud::HudFrame frame = hud::BuildFrame(hud->document, 1280, 720, session->Values());
    std::vector<std::string> texts;
    for (const hud::DrawItem& item : frame.items)
    {
        if (item.kind == hud::DrawKind::Text)
        {
            texts.push_back(item.text);
        }
    }
    EXPECT_NE(std::ranges::find(texts, "Coins 1/5"), texts.end());
    EXPECT_NE(std::ranges::find(texts, "Score 1"), texts.end());
    EXPECT_EQ(std::ranges::find(texts, "You collected every coin!"), texts.end()); // not yet

    for (const char* action : {"moveRight", "moveRight", "moveDown", "moveLeft"}) // on from x = 1
    {
        Press(*session, action);
    }
    const hud::HudFrame won = hud::BuildFrame(hud->document, 1280, 720, session->Values());
    bool shown = false;
    for (const hud::DrawItem& item : won.items)
    {
        shown = shown || item.text == "You collected every coin!";
    }
    EXPECT_TRUE(shown);
}

TEST_F(StarterSampleTest, Menus_NavigateAndRaiseGameActions)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    hud::MenuStack menus;
    for (const char* name : {"main_menu", "pause", "settings"})
    {
        const auto doc = hud::LoadHud(session->Files(), std::string("content/ui/") + name + ".hud.json");
        ASSERT_TRUE(doc.has_value()) << name << ": " << doc.error().message;
        menus.Add(name, doc->document);
    }
    ASSERT_TRUE(menus.Push("pause"));
    menus.Update(1280, 720, session->Values());
    // Down to Settings (the fourth button) with the keyboard, open it, go back.
    for (int i = 0; i < 4; ++i)
    {
        menus.Current()->FocusNext();
    }
    EXPECT_EQ(menus.Current()->Focused(), "settings");
    menus.Current()->Activate();
    EXPECT_TRUE(menus.TakeActions().empty());
    EXPECT_EQ(menus.Top(), "settings");
    menus.Update(1280, 720, session->Values());
    menus.Current()->FocusPrevious(); // wraps to the last button: Back
    menus.Current()->Activate();
    EXPECT_TRUE(menus.TakeActions().empty());
    EXPECT_EQ(menus.Top(), "pause");
    // Resume is the game's to handle.
    menus.Update(1280, 720, session->Values());
    menus.Current()->FocusNext(); // the pause menu remembers its focus (Settings): Quit, then Resume
    menus.Current()->FocusNext();
    menus.Current()->Activate();
    EXPECT_EQ(menus.TakeActions(), std::vector<std::string>{"game.resume"});

    // Every button action in every menu is one the game or the menu stack understands.
    for (const char* name : {"main_menu", "pause", "settings"})
    {
        const auto doc = hud::LoadHud(session->Files(), std::string("content/ui/") + name + ".hud.json");
        for (const hud::HudWidget& widget : doc->document.widgets)
        {
            if (widget.type == hud::WidgetType::Button && widget.action.starts_with("menu.push:"))
            {
                EXPECT_TRUE(menus.Has(widget.action.substr(std::string_view("menu.push:").size()))) << widget.action;
            }
        }
    }
}

TEST_F(StarterSampleTest, Input_BindsAndRebinds)
{
    auto session = Open();
    ASSERT_NE(session, nullptr);
    InputMapper mapper;
    const auto warnings = ApplyInputConfig(ReadText(m_project / "content" / "input.json"), mapper);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());
    // Every action the chains react to is bound.
    for (const char* action : {"moveLeft", "moveRight", "moveUp", "moveDown", "pause"})
    {
        EXPECT_TRUE(mapper.FindAction(action).has_value()) << action;
    }
    // The settings menu's button raises "input.rebind:moveRight": the prompt waits for the next key, which then
    // drives the game.
    RebindPrompt prompt;
    ASSERT_TRUE(prompt.BeginFromAction("input.rebind:moveRight"));
    const std::vector<InputEvent> pressed = {KeyEvent{Key::K, true, false}};
    const auto outcome = prompt.Update(pressed, mapper);
    ASSERT_EQ(outcome.status, RebindPrompt::Status::Rebound);
    mapper.Update(pressed);
    ASSERT_TRUE(mapper.WasPressed(*mapper.FindAction("moveRight")));
    session->RaiseInputAction("moveRight");
    session->Step();
    session->Step();
    EXPECT_EQ(PlayerPosition(*session), Vec3(1, 0, 0));
    // And the player's bindings save as a config that loads back.
    InputMapper reloaded;
    ASSERT_TRUE(ApplyInputConfig(CaptureInputConfig(mapper), reloaded, InputMerge::Replace).has_value());
    EXPECT_EQ(InputName(reloaded.ActionBindings(*reloaded.FindAction("moveRight"))[0]), "K");
}

TEST_F(StarterSampleTest, CameraRigs_ParseAndSwitch)
{
    const auto rigs = ParseCameraRigs(ReadText(m_project / "content" / "camera.json"));
    ASSERT_TRUE(rigs.has_value()) << rigs.error().message;
    EXPECT_TRUE(rigs->warnings.empty());
    CameraController camera(rigs->rigs, 0.0f);
    const CameraPose map = camera.Pose(Vec3(2, 0, 0));
    EXPECT_NEAR(map.eye.y, 10.0f, 1e-3f); // top-down, above the player
    EXPECT_TRUE(camera.SwitchTo("follow"));
    EXPECT_EQ(camera.TakeEvents().size(), 1u);
}
