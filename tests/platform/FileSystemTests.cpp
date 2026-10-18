// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using interi::ErrorCode;
using interi::MemoryFileSystem;
using interi::NativeFileSystem;
using interi::VirtualFileSystem;

namespace
{

std::vector<std::byte> Bytes(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return {data, data + text.size()};
}

std::string Text(const std::vector<std::byte>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

} // namespace

TEST(VirtualPath, Normalize_CollapsesSeparatorsAndDots)
{
    EXPECT_EQ(interi::NormalizeVirtualPath("a/b/c.txt").value(), "a/b/c.txt");
    EXPECT_EQ(interi::NormalizeVirtualPath("/a//b/./c.txt").value(), "a/b/c.txt");
    EXPECT_EQ(interi::NormalizeVirtualPath("a\\b\\c.txt").value(), "a/b/c.txt");
    EXPECT_EQ(interi::NormalizeVirtualPath("a/b/").value(), "a/b");
    EXPECT_EQ(interi::NormalizeVirtualPath("").value(), "");
}

TEST(VirtualPath, ParentReference_Rejected)
{
    const auto result = interi::NormalizeVirtualPath("a/../../etc/passwd");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(interi::NormalizeVirtualPath("..").has_value());
    EXPECT_FALSE(interi::NormalizeVirtualPath("C:/Windows").has_value());
}

TEST(MemoryFileSystem, WriteThenRead_Roundtrips)
{
    MemoryFileSystem fs;
    EXPECT_FALSE(fs.Exists("data/a.bin"));
    ASSERT_TRUE(fs.WriteFile("data/a.bin", Bytes("hello")).has_value());
    EXPECT_TRUE(fs.Exists("data/a.bin"));
    EXPECT_EQ(Text(fs.ReadFile("data/a.bin").value()), "hello");
}

TEST(MemoryFileSystem, ReadMissing_ReturnsNotFound)
{
    MemoryFileSystem fs;
    const auto result = fs.ReadFile("nope.txt");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(NativeFileSystem, WriteThenRead_Roundtrips)
{
    const interi::testing::ScopedTempDir dir;
    NativeFileSystem fs(dir.Path());
    ASSERT_TRUE(fs.WriteFile("sub/dir/file.txt", Bytes("native")).has_value());
    EXPECT_TRUE(fs.Exists("sub/dir/file.txt"));
    EXPECT_TRUE(std::filesystem::exists(dir.Path() / "sub" / "dir" / "file.txt"));
    EXPECT_EQ(Text(fs.ReadFile("sub/dir/file.txt").value()), "native");
}

TEST(NativeFileSystem, ReadMissing_ReturnsNotFound)
{
    const interi::testing::ScopedTempDir dir;
    NativeFileSystem fs(dir.Path());
    const auto result = fs.ReadFile("missing.bin");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(NativeFileSystem, EscapingPath_Rejected)
{
    const interi::testing::ScopedTempDir dir;
    NativeFileSystem fs(dir.Path() / "root");
    const auto result = fs.WriteFile("../outside.txt", Bytes("x"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(std::filesystem::exists(dir.Path() / "outside.txt"));
}

TEST(VirtualFS, MountOverride_LaterMountWins)
{
    MemoryFileSystem base;
    MemoryFileSystem patch;
    ASSERT_TRUE(base.WriteFile("textures/wall.dds", Bytes("base")).has_value());
    ASSERT_TRUE(patch.WriteFile("textures/wall.dds", Bytes("patch")).has_value());

    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("", base).has_value());
    ASSERT_TRUE(vfs.Mount("", patch).has_value());
    EXPECT_EQ(Text(vfs.ReadFile("textures/wall.dds").value()), "patch");
}

TEST(VirtualFS, Unmount_RemovesThatFileSystemOnly)
{
    MemoryFileSystem base;
    MemoryFileSystem patch;
    ASSERT_TRUE(base.WriteFile("textures/wall.dds", Bytes("base")).has_value());
    ASSERT_TRUE(patch.WriteFile("textures/wall.dds", Bytes("patch")).has_value());

    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("", base).has_value());
    ASSERT_TRUE(vfs.Mount("", patch).has_value());
    EXPECT_TRUE(vfs.Unmount(patch));
    EXPECT_FALSE(vfs.Unmount(patch));
    EXPECT_EQ(Text(vfs.ReadFile("textures/wall.dds").value()), "base");
    ASSERT_TRUE(vfs.WriteFile("new.txt", Bytes("x")).has_value());
    EXPECT_TRUE(base.Exists("new.txt"));
}

TEST(VirtualFS, LaterMountMissingFile_FallsThrough)
{
    MemoryFileSystem base;
    MemoryFileSystem patch;
    ASSERT_TRUE(base.WriteFile("a.txt", Bytes("base-a")).has_value());
    ASSERT_TRUE(patch.WriteFile("b.txt", Bytes("patch-b")).has_value());

    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("", base).has_value());
    ASSERT_TRUE(vfs.Mount("", patch).has_value());
    EXPECT_EQ(Text(vfs.ReadFile("a.txt").value()), "base-a");
    EXPECT_EQ(Text(vfs.ReadFile("b.txt").value()), "patch-b");
    EXPECT_FALSE(vfs.Exists("c.txt"));
    EXPECT_EQ(vfs.ReadFile("c.txt").error().code, ErrorCode::NotFound);
}

TEST(VirtualFS, MountPrefix_StrippedBeforeBackendLookup)
{
    MemoryFileSystem assets;
    ASSERT_TRUE(assets.WriteFile("meshes/tower.glb", Bytes("tower")).has_value());

    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("/game/", assets).has_value());
    EXPECT_TRUE(vfs.Exists("game/meshes/tower.glb"));
    EXPECT_EQ(Text(vfs.ReadFile("/game/meshes/tower.glb").value()), "tower");
    EXPECT_FALSE(vfs.Exists("meshes/tower.glb"));
    EXPECT_FALSE(vfs.Exists("gamex/meshes/tower.glb")); // prefix matches whole path components only
}

TEST(VirtualFS, Write_GoesToNewestMatchingMount)
{
    MemoryFileSystem base;
    MemoryFileSystem saves;
    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("", base).has_value());
    ASSERT_TRUE(vfs.Mount("saves", saves).has_value());

    ASSERT_TRUE(vfs.WriteFile("saves/slot0.sav", Bytes("s")).has_value());
    EXPECT_TRUE(saves.Exists("slot0.sav"));
    EXPECT_FALSE(base.Exists("saves/slot0.sav"));

    ASSERT_TRUE(vfs.WriteFile("config.ini", Bytes("c")).has_value());
    EXPECT_TRUE(base.Exists("config.ini"));
}

TEST(VirtualFS, NoMatchingMount_WriteFails)
{
    MemoryFileSystem saves;
    VirtualFileSystem vfs;
    ASSERT_TRUE(vfs.Mount("saves", saves).has_value());
    const auto result = vfs.WriteFile("other/file.txt", Bytes("x"));
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST(VirtualFS, BadPaths_Rejected)
{
    MemoryFileSystem base;
    VirtualFileSystem vfs;
    EXPECT_FALSE(vfs.Mount("../up", base).has_value());
    ASSERT_TRUE(vfs.Mount("", base).has_value());
    EXPECT_EQ(vfs.ReadFile("../x").error().code, ErrorCode::InvalidArgument);
}
