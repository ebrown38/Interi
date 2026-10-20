// SPDX-License-Identifier: MIT
#include <interi/platform/PrivateFiles.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// Creates a symlink, or skips the test where the platform refuses (Windows without the privilege).
#define INTERI_SYMLINK_OR_SKIP(create)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        std::error_code linkError;                                                                                     \
        create;                                                                                                        \
        if (linkError)                                                                                                 \
        {                                                                                                              \
            GTEST_SKIP() << "symlinks are not available: " << linkError.message();                                     \
        }                                                                                                              \
    } while (false)

TEST(PrivateFiles, RemoveFileBeneath_RegularFile_Removed)
{
    interi::testing::ScopedTempDir temp;
    fs::create_directories(temp.Path() / "imported/sub");
    std::ofstream(temp.Path() / "imported/sub/a.bin") << "x";
    const auto removed = RemoveFileBeneath(temp.Path(), "imported/sub/a.bin");
    ASSERT_TRUE(removed.has_value()) << removed.error().message;
    EXPECT_TRUE(*removed);
    EXPECT_FALSE(fs::exists(temp.Path() / "imported/sub/a.bin"));
    const auto again = RemoveFileBeneath(temp.Path(), "imported/sub/a.bin");
    ASSERT_TRUE(again.has_value());
    EXPECT_FALSE(*again); // already gone
    const auto noDirectory = RemoveFileBeneath(temp.Path(), "missing/a.bin");
    ASSERT_TRUE(noDirectory.has_value());
    EXPECT_FALSE(*noDirectory);
}

TEST(PrivateFiles, RemoveFileBeneath_NotNormalised_Refused)
{
    interi::testing::ScopedTempDir temp;
    std::vector<std::string> hostile = {"", "a//b", "../a", "a/./b", "a/", "/a", std::string("a\0b", 3)};
#if defined(_WIN32)
    hostile.insert(hostile.end(), {"a\\..\\b", "C:\\x", "a/b:stream"});
#endif
    for (const std::string& relative : hostile)
    {
        const auto removed = RemoveFileBeneath(temp.Path(), relative);
        ASSERT_FALSE(removed.has_value()) << relative;
        EXPECT_EQ(removed.error().code, ErrorCode::InvalidArgument) << relative;
    }
}

TEST(PrivateFiles, RemoveFileBeneath_Directory_Refused)
{
    interi::testing::ScopedTempDir temp;
    fs::create_directories(temp.Path() / "imported/sub");
    EXPECT_FALSE(RemoveFileBeneath(temp.Path(), "imported/sub").has_value());
    EXPECT_TRUE(fs::is_directory(temp.Path() / "imported/sub"));
}

TEST(PrivateFiles, RemoveFileBeneath_LeafSymlink_TargetSurvives)
{
    interi::testing::ScopedTempDir temp;
    interi::testing::ScopedTempDir outside;
    std::ofstream(outside.Path() / "victim") << "keep";
    fs::create_directories(temp.Path() / "imported");
    INTERI_SYMLINK_OR_SKIP(fs::create_symlink(outside.Path() / "victim", temp.Path() / "imported/a.bin", linkError));
    EXPECT_FALSE(RemoveFileBeneath(temp.Path(), "imported/a.bin").has_value());
    EXPECT_TRUE(fs::exists(outside.Path() / "victim"));
}

TEST(PrivateFiles, RemoveFileBeneath_SymlinkedDirectory_TargetSurvives)
{
    interi::testing::ScopedTempDir temp;
    interi::testing::ScopedTempDir outside;
    std::ofstream(outside.Path() / "victim") << "keep";
    fs::create_directories(temp.Path() / "imported");
    // The first component (the imported folder itself) and a directory below it are links out of the tree.
    INTERI_SYMLINK_OR_SKIP(fs::create_directory_symlink(outside.Path(), temp.Path() / "linked", linkError));
    INTERI_SYMLINK_OR_SKIP(fs::create_directory_symlink(outside.Path(), temp.Path() / "imported/sub", linkError));
    EXPECT_FALSE(RemoveFileBeneath(temp.Path(), "linked/victim").has_value());
    EXPECT_FALSE(RemoveFileBeneath(temp.Path(), "imported/sub/victim").has_value());
    EXPECT_TRUE(fs::exists(outside.Path() / "victim"));
}

TEST(PrivateFiles, RemoveFileBeneath_SymlinkedBase_Followed)
{
    // Only the components below the base are checked: the base (a project root) may legitimately be a link.
    interi::testing::ScopedTempDir temp;
    fs::create_directories(temp.Path() / "real/imported");
    std::ofstream(temp.Path() / "real/imported/a.bin") << "x";
    INTERI_SYMLINK_OR_SKIP(fs::create_directory_symlink(temp.Path() / "real", temp.Path() / "root", linkError));
    const auto removed = RemoveFileBeneath(temp.Path() / "root", "imported/a.bin");
    ASSERT_TRUE(removed.has_value()) << removed.error().message;
    EXPECT_TRUE(*removed);
    EXPECT_FALSE(fs::exists(temp.Path() / "real/imported/a.bin"));
}
