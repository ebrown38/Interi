// SPDX-License-Identifier: MIT
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <utility>

namespace fs = std::filesystem;
using interi::testing::ScopedTempDir;

TEST(ScopedTempDir, Constructor_CreatesEmptyDirectory)
{
    ScopedTempDir dir;
    ASSERT_TRUE(fs::is_directory(dir.Path()));
    EXPECT_TRUE(fs::is_empty(dir.Path()));
}

TEST(ScopedTempDir, Destructor_RemovesDirectory)
{
    fs::path path;
    {
        ScopedTempDir dir;
        path = dir.Path();
        std::ofstream(path / "file.txt") << "data";
        fs::create_directories(path / "nested" / "deeper");
        std::ofstream(path / "nested" / "deeper" / "file.bin") << "data";
    }
    EXPECT_FALSE(fs::exists(path));
}

TEST(ScopedTempDir, TwoInstances_HaveDistinctPaths)
{
    ScopedTempDir a;
    ScopedTempDir b;
    EXPECT_NE(a.Path(), b.Path());
}

TEST(ScopedTempDir, Move_TransfersOwnershipWithoutDeleting)
{
    fs::path path;
    {
        ScopedTempDir source;
        path = source.Path();
        ScopedTempDir destination(std::move(source));
        EXPECT_EQ(destination.Path(), path);
        EXPECT_TRUE(fs::is_directory(path)); // moved-from destructor must not delete it
    }
    EXPECT_FALSE(fs::exists(path));
}

TEST(ScopedTempDir, MoveAssign_DeletesOwnedDirectoryAndTakesSource)
{
    ScopedTempDir target;
    ScopedTempDir source;
    const fs::path oldTargetPath = target.Path();
    const fs::path sourcePath = source.Path();

    target = std::move(source);

    EXPECT_FALSE(fs::exists(oldTargetPath));
    EXPECT_EQ(target.Path(), sourcePath);
    EXPECT_TRUE(fs::is_directory(sourcePath));
}
