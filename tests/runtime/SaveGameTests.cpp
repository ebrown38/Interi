// SPDX-License-Identifier: MIT
#include <interi/runtime/SaveGame.h>

#include <gtest/gtest.h>

#include <cstring>

using namespace interi;

namespace
{

std::vector<std::byte> Bytes(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return {data, data + text.size()};
}

std::string Text(std::span<const std::byte> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// A section backed by a string the test owns.
SaveSectionDef Section(std::string id, std::uint32_t version, std::string& state, std::uint32_t* seenVersion = nullptr)
{
    return {.id = std::move(id),
            .version = version,
            .capture = [&state] { return Bytes(state); },
            .restore = [&state, seenVersion](std::span<const std::byte> data, std::uint32_t saved) -> Result<void>
            {
                if (seenVersion != nullptr)
                {
                    *seenVersion = saved;
                }
                state = Text(data);
                return {};
            }};
}

} // namespace

TEST(SaveGame, WriteRead_RoundTrips)
{
    SaveGame game;
    game.label = "Level 3";
    game.Set({.id = "b:two", .version = 4, .data = Bytes("22")});
    game.Set({.id = "a:one", .version = 1, .data = Bytes("1")});
    game.Set({.id = "a:one", .version = 2, .data = Bytes("11")}); // replaces
    ASSERT_EQ(game.sections.size(), 2u);
    EXPECT_EQ(game.sections[0].id, "a:one"); // sorted
    const auto read = ReadSave(WriteSave(game));
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(read->label, "Level 3");
    ASSERT_NE(read->Find("a:one"), nullptr);
    EXPECT_EQ(read->Find("a:one")->version, 2u);
    EXPECT_EQ(Text(read->Find("b:two")->data), "22");
    EXPECT_EQ(read->Find("c:none"), nullptr);
}

TEST(SaveGame, WriteIsDeterministic)
{
    SaveGame game;
    game.Set({.id = "a:x", .version = 1, .data = Bytes("data")});
    EXPECT_EQ(WriteSave(game), WriteSave(game));
}

TEST(SaveGame, Read_FlippedByte_IsCorrupt_AtEveryPosition)
{
    SaveGame game;
    game.label = "x";
    game.Set({.id = "a:x", .version = 1, .data = Bytes("payload")});
    const std::vector<std::byte> good = WriteSave(game);
    for (std::size_t i = 0; i < good.size(); ++i)
    {
        std::vector<std::byte> bad = good;
        bad[i] ^= std::byte{0x40};
        const auto read = ReadSave(bad);
        // A flip in the header tag or version reads as Corrupt/VersionMismatch; either way it must not load.
        EXPECT_FALSE(read.has_value() && read->label == "x" && read->sections.size() == 1 &&
                     Text(read->sections[0].data) == "payload")
            << "byte " << i;
    }
}

TEST(SaveGame, Read_TruncatedOrEmpty_Fails)
{
    SaveGame game;
    game.Set({.id = "a:x", .version = 1, .data = Bytes("payload")});
    std::vector<std::byte> bytes = WriteSave(game);
    EXPECT_FALSE(ReadSave({}).has_value());
    bytes.resize(bytes.size() - 3);
    EXPECT_FALSE(ReadSave(bytes).has_value());
}

TEST(SaveRegistry, CaptureApply_RoundTripsSections)
{
    std::string coins = "3";
    std::string pos = "1,2";
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:coins", 1, coins)).has_value());
    ASSERT_TRUE(registry.Add("engine", Section("player", 1, pos)).has_value());
    const SaveGame game = registry.Capture("slot");
    EXPECT_EQ(game.label, "slot");
    ASSERT_EQ(game.sections.size(), 2u);
    coins = "99";
    pos = "0,0";
    const auto report = registry.Apply(game);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->restored.size(), 2u);
    EXPECT_EQ(coins, "3");
    EXPECT_EQ(pos, "1,2");
}

TEST(SaveRegistry, Add_RejectsBadIdsAndDuplicates)
{
    std::string state;
    SaveRegistry registry;
    EXPECT_FALSE(registry.Add("com.a", Section("coins", 1, state)).has_value());       // not prefixed
    EXPECT_FALSE(registry.Add("com.a", Section("com.b:coins", 1, state)).has_value()); // someone else's prefix
    EXPECT_FALSE(registry.Add("com.a", Section("com.a:", 1, state)).has_value());      // empty name
    EXPECT_FALSE(registry.Add("engine", Section("a:b", 1, state)).has_value());        // engine ids have no ':'
    EXPECT_FALSE(registry.Add("com.a", Section("com.a:x", 0, state)).has_value());     // version 0
    SaveSectionDef noRestore = Section("com.a:y", 1, state);
    noRestore.restore = nullptr;
    EXPECT_FALSE(registry.Add("com.a", noRestore).has_value());
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:x", 1, state)).has_value());
    EXPECT_FALSE(registry.Add("com.a", Section("com.a:x", 1, state)).has_value()); // duplicate
}

TEST(SaveRegistry, OlderSection_ReceivesItsVersion)
{
    std::string state = "new";
    std::uint32_t seen = 0;
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:s", 3, state, &seen)).has_value());
    SaveGame game;
    game.Set({.id = "com.a:s", .version = 2, .data = Bytes("old")});
    ASSERT_TRUE(registry.Apply(game).has_value());
    EXPECT_EQ(seen, 2u);
    EXPECT_EQ(state, "old");
}

TEST(SaveRegistry, NewerSection_RefusesEverything)
{
    std::string a = "a";
    std::string b = "b";
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:a", 1, a)).has_value());
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:b", 1, b)).has_value());
    SaveGame game;
    game.Set({.id = "com.a:a", .version = 1, .data = Bytes("A")});
    game.Set({.id = "com.a:b", .version = 2, .data = Bytes("B")});
    const auto report = registry.Apply(game);
    ASSERT_FALSE(report.has_value());
    EXPECT_EQ(report.error().code, ErrorCode::VersionMismatch);
    EXPECT_EQ(a, "a"); // nothing applied
}

TEST(SaveRegistry, FailingRestore_IsReported_OthersStillApply)
{
    std::string ok = "ok";
    std::string bad = "bad";
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:ok", 1, ok)).has_value());
    SaveSectionDef failing = Section("com.a:bad", 1, bad);
    failing.restore = [](std::span<const std::byte>, std::uint32_t) -> Result<void>
    { return MakeError(ErrorCode::Corrupt, "nope"); };
    ASSERT_TRUE(registry.Add("com.a", failing).has_value());
    SaveGame game;
    game.Set({.id = "com.a:ok", .version = 1, .data = Bytes("restored")});
    game.Set({.id = "com.a:bad", .version = 1, .data = Bytes("x")});
    const auto report = registry.Apply(game);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(ok, "restored");
    ASSERT_EQ(report->failed.size(), 1u);
    EXPECT_NE(report->failed[0].find("com.a:bad"), std::string::npos);
}

TEST(SaveRegistry, UnknownSections_AreReportedAndSurviveRecapture)
{
    std::string state = "s";
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:s", 1, state)).has_value());
    SaveGame loaded;
    loaded.Set({.id = "com.gone:thing", .version = 7, .data = Bytes("keep me")});
    loaded.Set({.id = "com.a:s", .version = 1, .data = Bytes("x")});
    const auto report = registry.Apply(loaded);
    ASSERT_TRUE(report.has_value());
    ASSERT_EQ(report->unknown.size(), 1u);
    EXPECT_EQ(report->unknown[0], "com.gone:thing");
    const SaveGame again = registry.Capture("", &loaded);
    ASSERT_NE(again.Find("com.gone:thing"), nullptr);
    EXPECT_EQ(again.Find("com.gone:thing")->version, 7u);
    EXPECT_EQ(Text(again.Find("com.gone:thing")->data), "keep me");
    EXPECT_EQ(registry.Capture("").Find("com.gone:thing"), nullptr); // without `previous`: dropped
}

TEST(SaveRegistry, ExtractRestoreAndRemoveProvider)
{
    std::string state;
    SaveRegistry registry;
    ASSERT_TRUE(registry.Add("com.a", Section("com.a:s", 1, state)).has_value());
    ASSERT_TRUE(registry.Add("engine", Section("e", 1, state)).has_value());
    auto extracted = registry.ExtractProvider("com.a");
    EXPECT_FALSE(registry.Has("com.a:s"));
    EXPECT_TRUE(registry.Has("e"));
    registry.Restore(std::move(extracted));
    EXPECT_TRUE(registry.Has("com.a:s"));
    EXPECT_EQ(registry.RemoveProvider("com.a"), 1u);
    EXPECT_EQ(registry.RemoveProvider("com.a"), 0u);
    EXPECT_EQ(registry.Ids(), std::vector<std::string>{"e"});
}

TEST(SaveSlots, WriteReadList)
{
    MemoryFileSystem files;
    SaveSlots slots(files, "saves", 3);
    SaveGame game;
    game.label = "Level 1";
    game.Set({.id = "a:x", .version = 1, .data = Bytes("d")});
    ASSERT_TRUE(slots.Write(1, game).has_value());
    EXPECT_TRUE(slots.Exists(1));
    EXPECT_FALSE(slots.Exists(0));
    const auto read = slots.Read(1);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->label, "Level 1");
    EXPECT_EQ(slots.Read(0).error().code, ErrorCode::NotFound);
    const auto list = slots.List();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_FALSE(list[0].occupied);
    EXPECT_TRUE(list[1].occupied);
    EXPECT_EQ(list[1].label, "Level 1");
}

TEST(SaveSlots, OutOfRangeSlot_IsInvalid)
{
    MemoryFileSystem files;
    SaveSlots slots(files, "saves", 2);
    EXPECT_EQ(slots.Write(2, {}).error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(slots.Read(5).error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(slots.Exists(2));
}

TEST(SaveSlots, CorruptFile_ListsAsCorrupt)
{
    MemoryFileSystem files;
    SaveSlots slots(files, "saves", 2);
    const std::vector<std::byte> junk(20, std::byte{7});
    ASSERT_TRUE(files.WriteFile("saves/slot0.isav", junk).has_value());
    EXPECT_FALSE(slots.Read(0).has_value());
    const auto list = slots.List();
    EXPECT_TRUE(list[0].corrupt);
    EXPECT_FALSE(list[0].occupied);
}
