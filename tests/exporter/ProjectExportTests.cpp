// SPDX-License-Identifier: MIT
// Game export on the starter sample's project with its module reduced to content (no library), so the unit tests
// need no built module; tests/samples/StarterExportTests.cpp exports it with the real library and plays the package.
#include <interi/exporter/ProjectExport.h>
#include <interi/testing/ScopedTempDir.h>
#include <interi/values/ProjectData.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace interi;
namespace fs = std::filesystem;

namespace
{

const fs::path kStarter = INTERI_STARTER_SOURCE_DIR;

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void WriteText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

class ExportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_project = m_temp.Path() / "project";
        fs::copy(kStarter / "templates" / "coins", m_project, fs::copy_options::recursive);
        nlohmann::json manifest = nlohmann::json::parse(ReadText(kStarter / "module.json"));
        manifest.erase("library"); // content only
        WriteText(m_project / "modules" / "com.interi.starter" / "module.json", manifest.dump(2));
        m_desc.projectDirectory = m_project;
        m_desc.outputDirectory = m_temp.Path() / "out";
    }

    static bool Has(const exporter::ExportReport& report, const std::string& path)
    {
        return std::ranges::any_of(report.files, [&](const exporter::PackagedFile& f) { return f.path == path; });
    }

    interi::testing::ScopedTempDir m_temp;
    fs::path m_project;
    exporter::ExportDesc m_desc;
};

} // namespace

TEST_F(ExportTest, Project_ExportsContentModulesCookedDataAndLaunchers)
{
    auto report = exporter::ExportProject(m_desc);
    ASSERT_TRUE(report.has_value()) << report.error().message;
    EXPECT_EQ(report->gameName, "Coin Run");
    EXPECT_EQ(report->modules, std::vector<std::string>{"com.interi.starter"});
    for (const char* path : {"game/project.json", "game/project_data.json", "game/cooked.ival",
                             "game/content/scenes/main.scene.json", "game/content/chains/player.chain.json",
                             "game/modules/com.interi.starter/module.json", "coin_run.sh", "coin_run.bat"})
    {
        EXPECT_TRUE(Has(*report, path)) << path;
        EXPECT_TRUE(fs::exists(m_desc.outputDirectory / path)) << path;
    }
    EXPECT_FALSE(Has(*report, "package.json")) << "the manifest doesn't list itself";
    EXPECT_FALSE(fs::exists(m_desc.outputDirectory.string() + ".partial"));
}

TEST_F(ExportTest, CookedData_HoldsTheResolvedValuesAndTables)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    const std::string text = ReadText(m_desc.outputDirectory / "game" / "cooked.ival");
    auto cooked = values::ReadCookedData(std::as_bytes(std::span(text.data(), text.size())));
    ASSERT_TRUE(cooked.has_value()) << cooked.error().message;
    const values::Value* total = cooked->FindValue("com.interi.starter:total");
    ASSERT_NE(total, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(total->Variant()), 5);
    EXPECT_NE(cooked->FindTable("com.interi.starter:CoinKinds"), nullptr);
}

TEST_F(ExportTest, Package_Verifies)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    auto info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_TRUE(info.has_value()) << info.error().message;
    EXPECT_EQ(info->gameName, "Coin Run");
    EXPECT_EQ(info->modules, std::vector<std::string>{"com.interi.starter"});
    EXPECT_EQ(info->gameDirectory, m_desc.outputDirectory / "game");
    ASSERT_EQ(info->libraryDirectories.size(), 1u);
    EXPECT_TRUE(fs::exists(info->libraryDirectories[0] / "module.json"));
}

TEST_F(ExportTest, ChangedFile_FailsVerification)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    const fs::path file = m_desc.outputDirectory / "game" / "content" / "chains" / "player.chain.json";
    WriteText(file, ReadText(file) + " ");
    auto info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code, ErrorCode::Corrupt);
}

TEST_F(ExportTest, MissingFile_FailsVerification)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    fs::remove(m_desc.outputDirectory / "game" / "cooked.ival");
    auto info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code, ErrorCode::Corrupt);
}

TEST_F(ExportTest, ManifestListingAPathOutsideThePackage_IsRejected)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    const fs::path manifestPath = m_desc.outputDirectory / "package.json";
    nlohmann::json manifest = nlohmann::json::parse(ReadText(manifestPath));
    manifest["files"].push_back({{"path", "../outside.txt"}, {"size", 0}, {"hash", "0000000000000000"}});
    WriteText(manifestPath, manifest.dump());
    auto info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code, ErrorCode::Corrupt);
}

TEST_F(ExportTest, OtherToolchainOrNewerFormat_IsRefused)
{
    ASSERT_TRUE(exporter::ExportProject(m_desc).has_value());
    const fs::path manifestPath = m_desc.outputDirectory / "package.json";
    const nlohmann::json original = nlohmann::json::parse(ReadText(manifestPath));
    nlohmann::json manifest = original;
    manifest["toolchain"] = "some other compiler";
    WriteText(manifestPath, manifest.dump());
    auto info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code, ErrorCode::VersionMismatch);

    manifest = original;
    manifest["format"] = exporter::kPackageFormatVersion + 1;
    WriteText(manifestPath, manifest.dump());
    info = exporter::VerifyPackage(m_desc.outputDirectory);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code, ErrorCode::VersionMismatch);
}

TEST_F(ExportTest, NonEmptyOutput_IsRefusedAndLeftAlone)
{
    WriteText(m_desc.outputDirectory / "keep.txt", "mine");
    auto report = exporter::ExportProject(m_desc);
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(ReadText(m_desc.outputDirectory / "keep.txt"), "mine");
}

TEST_F(ExportTest, ModuleLibraryNotFound_FailsAndLeavesNoOutput)
{
    nlohmann::json manifest = nlohmann::json::parse(ReadText(kStarter / "module.json"));
    WriteText(m_project / "modules" / "com.interi.starter" / "module.json", manifest.dump(2)); // names its library
    auto report = exporter::ExportProject(m_desc);
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(fs::exists(m_desc.outputDirectory));
    EXPECT_FALSE(fs::exists(m_desc.outputDirectory.string() + ".partial"));
}

TEST_F(ExportTest, MissingStartupScene_Fails)
{
    fs::remove(m_project / "content" / "scenes" / "main.scene.json");
    auto report = exporter::ExportProject(m_desc);
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(fs::exists(m_desc.outputDirectory));
}

TEST_F(ExportTest, UnresolvableProject_Fails)
{
    fs::remove_all(m_project / "modules");
    auto report = exporter::ExportProject(m_desc);
    EXPECT_FALSE(report.has_value());
    EXPECT_FALSE(fs::exists(m_desc.outputDirectory));
}

TEST_F(ExportTest, RuntimeFiles_AreCopiedIntoBin)
{
    const fs::path fake = m_temp.Path() / "interi_player";
    WriteText(fake, "binary");
    m_desc.runtimeFiles = {fake};
    auto report = exporter::ExportProject(m_desc);
    ASSERT_TRUE(report.has_value()) << report.error().message;
    EXPECT_TRUE(Has(*report, "bin/interi_player"));
}
