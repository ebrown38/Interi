// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorModules.h>
#include <interi/editor/ScenePlay.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#ifdef INTERI_STARTER_LIBRARY_DIR

using namespace interi;
using namespace interi::editor;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{

// On the first step, the starter module's own action counts a finished level.
constexpr std::string_view kChain = R"json({"version": 1, "nodes": [
  {"id": "start", "event": "trigger.Start"},
  {"id": "done", "event": "com.interi.starter:LevelComplete"}],
  "links": [{"from": "start", "to": "done"}]})json";

class EditorModulesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!hotreload::ModuleHost::Supported())
        {
            GTEST_SKIP() << "static engine: modules can't load";
        }
        const fs::path source = INTERI_STARTER_SOURCE_DIR;
        m_folder = m_temp.Path() / "project";
        fs::copy(source / "templates" / "coins", m_folder, fs::copy_options::recursive);
        fs::create_directories(m_folder / "modules" / "com.interi.starter");
        fs::copy_file(source / "module.json", m_folder / "modules" / "com.interi.starter" / "module.json");
        auto project = sdk::LoadProject(m_folder);
        ASSERT_TRUE(project.has_value()) << project.error().message;
        m_project = std::move(*project);
        m_desc.librarySearchPaths = {fs::path(INTERI_STARTER_LIBRARY_DIR)};
        m_desc.shadowDirectory = m_temp.Path() / "shadow";
    }

    std::int32_t LevelsCompleted(EditorModules& modules)
    {
        const SaveGame game = modules.Host().Saves().Capture("");
        const SaveSection* section = game.Find("com.interi.starter:progress");
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
    fs::path m_folder;
    sdk::Project m_project;
    game::ProjectModulesDesc m_desc;
};

} // namespace

TEST_F(EditorModulesTest, Open_LoadsTheModulesAndTheirData)
{
    auto modules = EditorModules::Open(m_project, m_desc);
    ASSERT_TRUE(modules.has_value()) << modules.error().message;
    EXPECT_EQ((*modules)->Host().LoadedModules(), std::vector<std::string>{"com.interi.starter"});
    EXPECT_NE((*modules)->Data().Values().Definition("com.interi.starter:coins"), nullptr);
    EXPECT_EQ(
        (*modules)->Data().Tables().Get<std::int32_t>("com.interi.starter:CoinKinds", "silver", "value").value_or(0),
        5);
    // The module's native event and function are in the chain registry the editor plays with.
    EXPECT_NE((*modules)->Chains().Events().Resolve("com.interi.starter:LevelComplete"), nullptr);
    EXPECT_NE((*modules)->Chains().Functions().Resolve("com.interi.starter:CoinValue"), nullptr);
}

TEST_F(EditorModulesTest, Open_MissingLibrary_FailsLoadingNothing)
{
    game::ProjectModulesDesc nowhere = m_desc;
    nowhere.librarySearchPaths = {m_temp.Path() / "no_libraries_here"};
    const auto modules = EditorModules::Open(m_project, nowhere);
    ASSERT_FALSE(modules.has_value());
    EXPECT_EQ(modules.error().code, ErrorCode::NotFound);
}

TEST_F(EditorModulesTest, PlayInEditor_RunsTheModulesNativeEvents)
{
    auto modules = EditorModules::Open(m_project, m_desc);
    ASSERT_TRUE(modules.has_value()) << modules.error().message;
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("chains/finish.chain.json", std::as_bytes(std::span(kChain))).has_value());

    EditorDocument document;
    const EditorEntityId object = document.Scene().CreatePrimitive({}, {}, "finisher");
    ASSERT_TRUE(document.Scene().AddComponent(object, scene::ChainsComponent::kComponentName));
    document.Scene()
        .GetWorld()
        .GetMut<scene::ChainsComponent>(*document.Scene().Find(object))
        ->chains.items.emplace_back(values::AssetRef{"chains/finish.chain.json"});

    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    auto session =
        ScenePlaySession::Start(document.Scene(), (*modules)->Chains(), &(*modules)->Data().Values(), files, jobs);
    ASSERT_TRUE(session.has_value()) << session.error().message;
    EXPECT_TRUE((*session)->Runtime().Warnings().empty()) << (*session)->Runtime().Warnings().front();
    EXPECT_EQ(LevelsCompleted(**modules), 0);
    (*session)->Advance(20ms);
    EXPECT_EQ(LevelsCompleted(**modules), 1);
}

TEST_F(EditorModulesTest, Reload_KeepsTheProject)
{
    auto modules = EditorModules::Open(m_project, m_desc);
    ASSERT_TRUE(modules.has_value());
    const auto stats = (*modules)->Reload("com.interi.starter");
    ASSERT_TRUE(stats.has_value()) << stats.error().message;
    EXPECT_EQ(stats->reloadedModules, 1u);
    EXPECT_NE((*modules)->Chains().Events().Resolve("com.interi.starter:LevelComplete"), nullptr);
}

#endif
