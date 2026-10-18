// SPDX-License-Identifier: MIT
#include <interi/hud/HudDocument.h>
#include <interi/platform/FileSystem.h>

#include "HudTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::hud;

namespace
{

constexpr const char* kDoc = R"json({
  "version": 1, "name": "hud", "referenceSize": [1000, 500],
  "widgets": [
    { "id": "score", "type": "text", "anchor": "topLeft", "offset": [10, 10], "size": [200, 30],
      "text": "Coins {coins}/{total} {ratio|2}", "color": [1, 0.5, 0.25, 1] },
    { "id": "health", "type": "bar", "anchor": "bottomLeft", "offset": [10, -10], "size": [200, 20],
      "bind": "health", "min": 0, "max": 100, "visibleIf": "alive" },
    { "id": "icon", "type": "image", "image": "ui/coin.png" },
    { "id": "play", "type": "button", "anchor": "center", "text": "Play", "action": "menu.pop" },
    { "id": "items", "type": "list", "bind": "inventory", "rowHeight": 18 }
  ]})json";

} // namespace

TEST(HudDocument, Parse_ReadsEveryWidgetType)
{
    const auto parsed = ParseHud(kDoc);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    const HudDocument& doc = parsed->document;
    ASSERT_EQ(doc.widgets.size(), 5u);
    EXPECT_EQ(doc.referenceSize[0], 1000.0f);
    EXPECT_EQ(doc.widgets[0].type, WidgetType::Text);
    EXPECT_EQ(doc.widgets[1].anchor, Anchor::BottomLeft);
    EXPECT_EQ(doc.widgets[1].visibleIf, "alive");
    EXPECT_EQ(doc.widgets[3].action, "menu.pop");
    EXPECT_EQ(doc.widgets[4].rowHeight, 18.0f);
    EXPECT_NE(doc.Find("icon"), nullptr);
    EXPECT_EQ(doc.Find("nope"), nullptr);
    EXPECT_TRUE(parsed->warnings.empty());
}

TEST(HudDocument, Write_RoundTrips)
{
    const HudDocument doc = hudtest::MustParse(kDoc);
    const std::string text = WriteHud(doc);
    const HudDocument again = hudtest::MustParse(text);
    EXPECT_EQ(WriteHud(again), text);
    EXPECT_EQ(again.widgets.size(), doc.widgets.size());
    EXPECT_EQ(again.widgets[0].text, doc.widgets[0].text);
}

TEST(HudDocument, BoundValues_CollectsBindsVisibilityAndPlaceholders)
{
    const HudDocument doc = hudtest::MustParse(kDoc);
    const std::vector<std::string> expected = {"alive", "coins", "health", "inventory", "ratio", "total"};
    EXPECT_EQ(BoundValues(doc), expected);
}

TEST(HudDocument, Parse_Malformed_Fails)
{
    EXPECT_FALSE(ParseHud("[]"));
    EXPECT_FALSE(ParseHud(R"({"widgets": []})")); // no version
    EXPECT_EQ(ParseHud(R"({"version": 2, "widgets": []})").error().code, ErrorCode::VersionMismatch);
    const auto widget = [](const char* body)
    { return ParseHud(std::string(R"({"version":1,"widgets":[)") + body + "]}"); };
    EXPECT_FALSE(widget(R"({"type":"text"})"));
    EXPECT_FALSE(widget(R"({"id":"a"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"gauge"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"text","anchor":"middle"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"text","size":[1]})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"text","size":[-1,2]})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"text","color":[1,1,1]})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"bar"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"bar","bind":"x","min":5,"max":1})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"button"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"image"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"list"})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"list","bind":"x","rowHeight":0})"));
    EXPECT_FALSE(widget(R"({"id":"a","type":"text"},{"id":"a","type":"text"})")); // duplicate
}

TEST(HudDocument, Parse_UnknownFields_Warn)
{
    const auto parsed = ParseHud(R"({"version":1,"theme":"x","widgets":[{"id":"a","type":"text","blink":true}]})");
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->warnings.size(), 2u);
}

TEST(HudDocument, Load_ReadsFromFileSystem_AndNamesThePathOnError)
{
    MemoryFileSystem files;
    const std::string_view text = kDoc;
    ASSERT_TRUE(files.WriteFile("ui/hud.hud.json", std::as_bytes(std::span(text))).has_value());
    EXPECT_TRUE(LoadHud(files, "ui/hud.hud.json").has_value());
    EXPECT_EQ(LoadHud(files, "ui/missing.hud.json").error().code, ErrorCode::NotFound);
    const std::string_view bad = "{";
    ASSERT_TRUE(files.WriteFile("ui/bad.hud.json", std::as_bytes(std::span(bad))).has_value());
    const auto failed = LoadHud(files, "ui/bad.hud.json");
    ASSERT_FALSE(failed.has_value());
    EXPECT_NE(failed.error().message.find("ui/bad.hud.json"), std::string::npos);
}
