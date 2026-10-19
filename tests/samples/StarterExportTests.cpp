// SPDX-License-Identifier: MIT
// The starter sample exported with its real library, then run by interi_player (a process, like a player of the
// shipped game would): the package verifies, the game plays and the coins are collected from the packaged files.
#include <interi/exporter/ProjectExport.h>
#include <interi/game/GameSession.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

using namespace interi;
namespace fs = std::filesystem;

namespace
{

const fs::path kSource = INTERI_STARTER_SOURCE_DIR;
const fs::path kLibraries = INTERI_STARTER_LIBRARY_DIR;
const fs::path kPlayer = INTERI_PLAYER_EXECUTABLE;
const fs::path kExportTool = INTERI_EXPORT_EXECUTABLE;

// Runs `command`, returns its exit code and output.
int RunProcess(const std::string& command, std::string& output)
{
    output.clear();
#if defined(_WIN32)
    FILE* pipe = _popen(("\"" + command + " 2>&1\"").c_str(), "r");
#else
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
#endif
    if (pipe == nullptr)
    {
        return -1;
    }
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr)
    {
        output += buffer;
    }
#if defined(_WIN32)
    return _pclose(pipe);
#else
    const int status = pclose(pipe);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

class StarterExportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_project = m_temp.Path() / "project";
        fs::copy(kSource / "templates" / "coins", m_project, fs::copy_options::recursive);
        fs::create_directories(m_project / "modules" / "com.interi.starter");
        fs::copy_file(kSource / "module.json", m_project / "modules" / "com.interi.starter" / "module.json");
        m_package = m_temp.Path() / "package";
    }

    interi::testing::ScopedTempDir m_temp;
    fs::path m_project;
    fs::path m_package;
};

} // namespace

TEST_F(StarterExportTest, Package_OpensAsAGameSessionAndCollectsTheCoins)
{
    exporter::ExportDesc desc;
    desc.projectDirectory = m_project;
    desc.outputDirectory = m_package;
    desc.librarySearchPaths = {kLibraries};
    auto report = exporter::ExportProject(desc);
    ASSERT_TRUE(report.has_value()) << report.error().message;

    // The library came along into the module's folder in the package.
    bool library = false;
    for (const exporter::PackagedFile& file : report->files)
    {
        library = library || (file.path.starts_with("game/modules/com.interi.starter/") &&
                              file.path.find("com_interi_starter") != std::string::npos);
    }
    EXPECT_TRUE(library);

    auto info = exporter::VerifyPackage(m_package);
    ASSERT_TRUE(info.has_value()) << info.error().message;
    game::GameDesc game;
    game.projectDirectory = info->gameDirectory;
    game.modules.librarySearchPaths = info->libraryDirectories; // only the package: not the build directory
    game.modules.shadowDirectory = m_temp.Path() / "shadow";
    game.workerCount = 2;
    auto session = game::GameSession::Open(game);
    ASSERT_TRUE(session.has_value()) << session.error().message;
    for (const char* action : {"moveRight", "moveRight", "moveRight", "moveDown", "moveLeft"})
    {
        (*session)->RaiseInputAction(action);
        for (int i = 0; i < 4; ++i)
        {
            (*session)->Step();
        }
    }
    const values::Value* won = (*session)->Values().Read("com.interi.starter:won");
    ASSERT_NE(won, nullptr);
    EXPECT_TRUE(std::get<bool>(won->Variant()));
}

TEST_F(StarterExportTest, ExportToolAndPlayer_ExportThenPlayThePackage)
{
    std::string output;
    ASSERT_EQ(RunProcess("\"" + kExportTool.string() + "\" \"" + m_project.string() + "\" \"" + m_package.string() +
                             "\" --lib \"" + kLibraries.string() + "\" --runtime \"" + kPlayer.string() + "\"",
                         output),
              0)
        << output;
    EXPECT_NE(output.find("Exported 'Coin Run'"), std::string::npos) << output;
    EXPECT_TRUE(fs::exists(m_package / "bin" / kPlayer.filename()));

    const std::string player = "\"" + kPlayer.string() + "\" --package \"" + m_package.string() + "\"";
    ASSERT_EQ(RunProcess(player + " --verify-only", output), 0) << output;
    EXPECT_NE(output.find("verified"), std::string::npos);

    ASSERT_EQ(RunProcess(player +
                             " --steps 40 --press moveRight@0 --press moveRight@4 --press moveRight@8 --press "
                             "moveDown@12 --press moveLeft@16 --print com.interi.starter:coins --print "
                             "com.interi.starter:won --workers 2 --user-dir \"" +
                             (m_temp.Path() / "user").string() + "\"",
                         output),
              0)
        << output;
    EXPECT_NE(output.find("Played 40 steps"), std::string::npos) << output;
    EXPECT_NE(output.find("com.interi.starter:won=true"), std::string::npos) << output;
}

TEST_F(StarterExportTest, Player_RunsInAWindowOnTheDummyVideoDriver)
{
    exporter::ExportDesc desc;
    desc.projectDirectory = m_project;
    desc.outputDirectory = m_package;
    desc.librarySearchPaths = {kLibraries};
    ASSERT_TRUE(exporter::ExportProject(desc).has_value());
    std::string output;
    ASSERT_EQ(RunProcess("\"" + kPlayer.string() + "\" --package \"" + m_package.string() +
                             "\" --window --video-driver dummy --frames 30 --print com.interi.starter:total",
                         output),
              0)
        << output;
    EXPECT_NE(output.find("com.interi.starter:total=5"), std::string::npos) << output;
}

TEST_F(StarterExportTest, Player_RefusesATamperedPackage)
{
    exporter::ExportDesc desc;
    desc.projectDirectory = m_project;
    desc.outputDirectory = m_package;
    desc.librarySearchPaths = {kLibraries};
    ASSERT_TRUE(exporter::ExportProject(desc).has_value());
    std::ofstream(m_package / "game" / "project_data.json", std::ios::app) << " ";
    std::string output;
    EXPECT_EQ(RunProcess("\"" + kPlayer.string() + "\" --package \"" + m_package.string() + "\" --steps 1", output), 1)
        << output;
    EXPECT_NE(output.find("changed or is damaged"), std::string::npos) << output;
}

TEST(PlayerCommandLine, MissingPackage_IsAUsageError)
{
    std::string output;
    EXPECT_EQ(RunProcess("\"" + kPlayer.string() + "\"", output), 2) << output;
    EXPECT_EQ(RunProcess("\"" + kPlayer.string() + "\" --package /does/not/exist", output), 1) << output;
}
