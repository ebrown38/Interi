// SPDX-License-Identifier: MIT
#include <interi/platform/PrivateFiles.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace interi;
namespace fs = std::filesystem;

TEST(PrivateFiles, MissingDirectory_CreatedPrivate)
{
    interi::testing::ScopedTempDir temp;
    const fs::path dir = temp.Path() / "a" / "modules";
    ASSERT_TRUE(EnsurePrivateDirectory(dir).has_value());
    EXPECT_TRUE(fs::is_directory(dir));
#if !defined(_WIN32)
    EXPECT_EQ(fs::status(dir).permissions() & (fs::perms::group_all | fs::perms::others_all), fs::perms::none);
#endif
    EXPECT_TRUE(EnsurePrivateDirectory(dir).has_value()); // again: fine
}

#if !defined(_WIN32)
TEST(PrivateFiles, SymlinkedDirectory_Refused)
{
    interi::testing::ScopedTempDir temp;
    fs::create_directory(temp.Path() / "real");
    fs::create_directory_symlink(temp.Path() / "real", temp.Path() / "link");
    EXPECT_FALSE(EnsurePrivateDirectory(temp.Path() / "link").has_value());
}

TEST(PrivateFiles, WorldWritableDirectory_Refused)
{
    interi::testing::ScopedTempDir temp;
    const fs::path dir = temp.Path() / "open";
    fs::create_directory(dir);
    fs::permissions(dir, fs::perms::all);
    EXPECT_FALSE(EnsurePrivateDirectory(dir).has_value());
    fs::permissions(dir, fs::perms::owner_all);
    EXPECT_TRUE(EnsurePrivateDirectory(dir).has_value());
}

TEST(PrivateFiles, CopyOverSymlink_Refused)
{
    interi::testing::ScopedTempDir temp;
    std::ofstream(temp.Path() / "src") << "payload";
    std::ofstream(temp.Path() / "victim") << "keep";
    fs::create_symlink(temp.Path() / "victim", temp.Path() / "dst");
    EXPECT_FALSE(CopyFileExclusive(temp.Path() / "src", temp.Path() / "dst").has_value());
    std::ifstream in(temp.Path() / "victim");
    std::string text;
    in >> text;
    EXPECT_EQ(text, "keep");
}
#endif

TEST(PrivateFiles, CopyToExistingFile_Refused)
{
    interi::testing::ScopedTempDir temp;
    std::ofstream(temp.Path() / "src") << "payload";
    ASSERT_TRUE(CopyFileExclusive(temp.Path() / "src", temp.Path() / "dst").has_value());
    EXPECT_EQ(fs::file_size(temp.Path() / "dst"), 7u);
    EXPECT_FALSE(CopyFileExclusive(temp.Path() / "src", temp.Path() / "dst").has_value());
}
