// SPDX-License-Identifier: MIT
#include <interi/loc/Localization.h>

#include <gtest/gtest.h>

#include <span>

using namespace interi;
using namespace interi::loc;

namespace
{

StringTable Table(std::string language, std::map<std::string, std::string, std::less<>> strings)
{
    return {.language = std::move(language), .strings = std::move(strings)};
}

Localization MakeLocalization()
{
    Localization localization("en");
    EXPECT_TRUE(localization
                    .AddTable("com.test.game", Table("en", {{"menu.play", "Play"},
                                                            {"hud.coins", "Coins {coins}/{total}"},
                                                            {"menu.quit", "Quit"}}))
                    .has_value());
    EXPECT_TRUE(
        localization.AddTable("com.test.game", Table("fr", {{"menu.play", "Jouer"}, {"hud.coins", "Pièces {coins}"}}))
            .has_value());
    return localization;
}

} // namespace

TEST(Localization, DefaultLanguageIsTheFallback)
{
    const Localization localization = MakeLocalization();
    EXPECT_EQ(localization.Language(), "en");
    EXPECT_EQ(localization.Translate("menu.play"), "Play");
}

TEST(Localization, SetLanguage_SwitchesAndFallsBackPerKey)
{
    Localization localization = MakeLocalization();
    ASSERT_TRUE(localization.SetLanguage("fr").has_value());
    EXPECT_EQ(localization.Translate("menu.play"), "Jouer");
    EXPECT_EQ(localization.Translate("menu.quit"), "Quit"); // only in English: falls back
    EXPECT_EQ(localization.Translate("menu.unknown"), "menu.unknown");
    EXPECT_FALSE(localization.Find("menu.unknown").has_value());
}

TEST(Localization, SetLanguage_Unknown_KeepsTheCurrentOne)
{
    Localization localization = MakeLocalization();
    ASSERT_TRUE(localization.SetLanguage("fr").has_value());
    const auto result = localization.SetLanguage("de");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_EQ(localization.Language(), "fr");
}

TEST(Localization, Languages_AreSorted)
{
    const Localization localization = MakeLocalization();
    EXPECT_EQ(localization.Languages(), (std::vector<std::string>{"en", "fr"}));
}

TEST(Localization, LaterTableWinsPerKey)
{
    Localization localization = MakeLocalization();
    ASSERT_TRUE(localization.AddTable("project", Table("en", {{"menu.play", "Start"}})).has_value());
    EXPECT_EQ(localization.Translate("menu.play"), "Start");
    EXPECT_EQ(localization.Translate("menu.quit"), "Quit");
    localization.RemoveProvider("project");
    EXPECT_EQ(localization.Translate("menu.play"), "Play");
}

TEST(Localization, Format_ReplacesNamedArguments)
{
    Localization localization = MakeLocalization();
    ASSERT_TRUE(localization.AddTable("p", Table("en", {{"greet", "Hi {name}, {{x} {other}"}})).has_value());
    const std::pair<std::string_view, std::string_view> args[] = {{"name", "Ada"}};
    EXPECT_EQ(localization.Format("greet", args), "Hi Ada, {x} {other}");
}

TEST(Localization, MissingKeys_ListsWhatALanguageLacks)
{
    const Localization localization = MakeLocalization();
    EXPECT_EQ(localization.MissingKeys("fr"), (std::vector<std::string>{"menu.quit"}));
    EXPECT_TRUE(localization.MissingKeys("en").empty());
    EXPECT_EQ(localization.MissingKeys("de").size(), 3u);
}

TEST(Localization, AddTable_BadLanguageOrKey_Rejected)
{
    Localization localization;
    EXPECT_FALSE(localization.AddTable("p", Table("english!", {})).has_value());
    EXPECT_FALSE(localization.AddTable("p", Table("en", {{"bad key", "x"}})).has_value());
    EXPECT_FALSE(localization.AddTable("p", Table("en", {{"", "x"}})).has_value());
}

TEST(Localization, LanguageCodes)
{
    EXPECT_TRUE(IsValidLanguage("en"));
    EXPECT_TRUE(IsValidLanguage("pt-BR"));
    EXPECT_TRUE(IsValidLanguage("zh-Hant-TW"));
    EXPECT_FALSE(IsValidLanguage(""));
    EXPECT_FALSE(IsValidLanguage("e"));
    EXPECT_FALSE(IsValidLanguage("en-"));
    EXPECT_FALSE(IsValidLanguage("en_US"));
}

TEST(Localization, ApplyLanguageAction_SwitchesOrIgnores)
{
    Localization localization = MakeLocalization();
    EXPECT_TRUE(ApplyLanguageAction(localization, "language.set:fr"));
    EXPECT_EQ(localization.Language(), "fr");
    EXPECT_FALSE(ApplyLanguageAction(localization, "language.set:xx"));
    EXPECT_FALSE(ApplyLanguageAction(localization, "menu.pop"));
    EXPECT_EQ(localization.Language(), "fr");
}

TEST(StringTable, Json_RoundTripsWithUtf8)
{
    const StringTable table = Table("fr", {{"a", "é à ü 日本"}, {"b.c", "x"}});
    const auto parsed = ParseStringTable(WriteStringTable(table));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, table);
}

TEST(StringTable, Parse_Malformed_Rejected)
{
    EXPECT_FALSE(ParseStringTable("nope").has_value());
    EXPECT_FALSE(ParseStringTable(R"({"version":1,"strings":{}})").has_value());
    EXPECT_FALSE(ParseStringTable(R"({"version":1,"language":"en"})").has_value());
    EXPECT_FALSE(ParseStringTable(R"({"version":1,"language":"en","strings":{"a":1}})").has_value());
    EXPECT_FALSE(ParseStringTable(R"({"version":1,"language":"en","strings":{"a b":"x"}})").has_value());
    EXPECT_EQ(ParseStringTable(R"({"version":2,"language":"en","strings":{}})").error().code,
              ErrorCode::VersionMismatch);
}

TEST(LoadModuleStrings, ReadsTheManifestsTables)
{
    MemoryFileSystem files;
    const auto put = [&](const std::string& path, const StringTable& table)
    {
        const std::string text = WriteStringTable(table);
        ASSERT_TRUE(files.WriteFile(path, std::as_bytes(std::span(text))).has_value());
    };
    put("i18n/en.strings.json", Table("en", {{"menu.play", "Play"}}));
    put("i18n/fr.strings.json", Table("fr", {{"menu.play", "Jouer"}}));
    const auto manifest = sdk::ParseManifest(R"({"id": "com.test.game", "version": "1.0.0",
        "localization": ["i18n/en.strings.json", "i18n/fr.strings.json"]})");
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;
    ASSERT_EQ(manifest->localization.size(), 2u);

    Localization localization;
    ASSERT_TRUE(LoadModuleStrings(*manifest, files, localization).has_value());
    EXPECT_TRUE(localization.SetLanguage("fr").has_value());
    EXPECT_EQ(localization.Translate("menu.play"), "Jouer");

    Localization missing;
    MemoryFileSystem empty;
    EXPECT_FALSE(LoadModuleStrings(*manifest, empty, missing).has_value());
}

TEST(Manifest, Localization_BadPaths_Rejected)
{
    EXPECT_FALSE(
        sdk::ParseManifest(R"({"id": "com.test.game", "version": "1.0.0", "localization": ["../x"]})").has_value());
    EXPECT_FALSE(sdk::ParseManifest(R"({"id": "com.test.game", "version": "1.0.0", "localization": [3]})").has_value());
}
