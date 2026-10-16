// SPDX-License-Identifier: MIT
#include <interi/hotreload/FileWatcher.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

void WriteFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

} // namespace

TEST(FileWatcher, FirstPoll_ReportsNoChange)
{
    const interi::testing::ScopedTempDir temp;
    WriteFile(temp.Path() / "a.txt", "one");
    interi::hotreload::FileWatcher watcher;
    watcher.Watch(temp.Path());
    EXPECT_FALSE(watcher.Poll());
    EXPECT_FALSE(watcher.Poll());
}

TEST(FileWatcher, Modified_Fires)
{
    const interi::testing::ScopedTempDir temp;
    const std::filesystem::path file = temp.Path() / "module.so";
    WriteFile(file, "one");
    interi::hotreload::FileWatcher watcher;
    watcher.Watch(file);
    (void)watcher.Poll();
    WriteFile(file, "one two"); // new size (modification times can be coarse)
    EXPECT_TRUE(watcher.Poll());
    EXPECT_FALSE(watcher.Poll());
}

TEST(FileWatcher, DirectoryAddRemove_Fires)
{
    const interi::testing::ScopedTempDir temp;
    std::filesystem::create_directories(temp.Path() / "src" / "nested");
    interi::hotreload::FileWatcher watcher;
    watcher.Watch(temp.Path() / "src");
    (void)watcher.Poll();
    WriteFile(temp.Path() / "src" / "nested" / "System.cpp", "x");
    EXPECT_TRUE(watcher.Poll());
    std::filesystem::remove(temp.Path() / "src" / "nested" / "System.cpp");
    EXPECT_TRUE(watcher.Poll());
    EXPECT_FALSE(watcher.Poll());
}

TEST(FileWatcher, MissingPath_AppearsLater_Fires)
{
    const interi::testing::ScopedTempDir temp;
    interi::hotreload::FileWatcher watcher;
    watcher.Watch(temp.Path() / "later.so");
    EXPECT_FALSE(watcher.Poll());
    WriteFile(temp.Path() / "later.so", "built");
    EXPECT_TRUE(watcher.Poll());
}
