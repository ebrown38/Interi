// SPDX-License-Identifier: MIT
#include <interi/hud/HudLayout.h>

#include "HudTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::hud;

namespace
{

constexpr const char* kDoc = R"json({
  "version": 1, "referenceSize": [100, 50],
  "widgets": [
    { "id": "tl", "type": "text", "anchor": "topLeft", "offset": [4, 2], "size": [40, 10], "text": "x" },
    { "id": "br", "type": "text", "anchor": "bottomRight", "offset": [-4, -2], "size": [40, 10], "text": "y" },
    { "id": "mid", "type": "text", "anchor": "center", "size": [20, 10], "text": "z" },
    { "id": "bar", "type": "bar", "anchor": "top", "size": [60, 6], "bind": "health", "min": 0, "max": 200,
      "visibleIf": "alive" }
  ]})json";

const DrawItem* FindItem(const HudFrame& frame, std::string_view widget, DrawKind kind)
{
    for (const DrawItem& item : frame.items)
    {
        if (item.widget == widget && item.kind == kind)
        {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

TEST(HudLayout, Anchors_PlaceFromTheScreenEdges)
{
    hudtest::MapValues vals;
    const HudFrame frame = BuildFrame(hudtest::MustParse(kDoc), 100.0f, 50.0f, vals);
    EXPECT_EQ(FindItem(frame, "tl", DrawKind::Text)->rect, (Rect{4, 2, 40, 10}));
    EXPECT_EQ(FindItem(frame, "br", DrawKind::Text)->rect, (Rect{100 - 40 - 4, 50 - 10 - 2, 40, 10}));
    EXPECT_EQ(FindItem(frame, "mid", DrawKind::Text)->rect, (Rect{40, 20, 20, 10}));
}

TEST(HudLayout, Scale_UsesTheSmallerRatio)
{
    hudtest::MapValues vals;
    const HudFrame frame = BuildFrame(hudtest::MustParse(kDoc), 200.0f, 200.0f, vals);
    // 200/100 = 2 and 200/50 = 4: scale 2.
    EXPECT_EQ(FindItem(frame, "tl", DrawKind::Text)->rect, (Rect{8, 4, 80, 20}));
}

TEST(HudLayout, Bar_FillsByValue_AndIsHiddenByVisibleIf)
{
    hudtest::MapValues vals;
    EXPECT_EQ(FindItem(BuildFrame(hudtest::MustParse(kDoc), 100, 50, vals), "bar", DrawKind::Rect), nullptr);
    vals.Set("alive", true);
    vals.Set("health", 50.0f);
    HudFrame frame = BuildFrame(hudtest::MustParse(kDoc), 100, 50, vals);
    // Background first, then the fill: a quarter of 60.
    std::vector<const DrawItem*> rects;
    for (const DrawItem& item : frame.items)
    {
        if (item.widget == "bar")
        {
            rects.push_back(&item);
        }
    }
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_FLOAT_EQ(rects[0]->rect.w, 60.0f);
    EXPECT_FLOAT_EQ(rects[1]->rect.w, 15.0f);
    vals.Set("health", 9999.0f); // clamps
    frame = BuildFrame(hudtest::MustParse(kDoc), 100, 50, vals);
    EXPECT_FLOAT_EQ(frame.items.back().rect.w, 60.0f);
    vals.Set("health", 0); // a wrong type: empty
    frame = BuildFrame(hudtest::MustParse(kDoc), 100, 50, vals);
    EXPECT_EQ(frame.items.back().rect.w, 60.0f); // only the background remains
}

TEST(HudLayout, FormatText_SubstitutesValues)
{
    hudtest::MapValues vals;
    vals.Set("coins", std::int32_t{3});
    vals.Set("ratio", 0.123456f);
    vals.Set("name", std::string("Bob"));
    vals.Set("on", true);
    EXPECT_EQ(FormatHudText("{coins} coins", vals), "3 coins");
    EXPECT_EQ(FormatHudText("{ratio|2}", vals), "0.12");
    EXPECT_EQ(FormatHudText("{coins|1}", vals), "3.0");
    EXPECT_EQ(FormatHudText("hi {name}, {on}", vals), "hi Bob, true");
    EXPECT_EQ(FormatHudText("{missing}!", vals), "?!");
    EXPECT_EQ(FormatHudText("{{literal} {coins", vals), "{literal} {coins");
    EXPECT_EQ(FormatHudText("no placeholders", vals), "no placeholders");
}

TEST(HudLayout, List_ShowsOneRowPerItemUpToItsHeight)
{
    const HudDocument doc = hudtest::MustParse(R"json({"version":1,"referenceSize":[100,100],"widgets":[
      {"id":"l","type":"list","bind":"items","size":[50,40],"rowHeight":20}]})json");
    hudtest::MapValues vals;
    values::ValueList list{.element = values::ValueType::Int, .items = {}};
    for (std::int32_t i = 1; i <= 5; ++i)
    {
        list.items.emplace_back(i);
    }
    vals.Set("items", list);
    const HudFrame frame = BuildFrame(doc, 100, 100, vals);
    ASSERT_EQ(frame.items.size(), 2u); // 40 / 20 rows
    EXPECT_EQ(frame.items[0].text, "1");
    EXPECT_EQ(frame.items[1].text, "2");
    EXPECT_EQ(frame.items[1].rect.y, 20.0f);
}

TEST(HudLayout, Button_HighlightsHoverFocusAndPress)
{
    const HudDocument doc = hudtest::MustParse(R"json({"version":1,"widgets":[
      {"id":"b","type":"button","size":[100,40],"text":"Go","action":"go"}]})json");
    hudtest::MapValues vals;
    const auto background = [&](HudHighlight highlight)
    { return BuildFrame(doc, 1280, 720, vals, highlight).items.front().color; };
    const values::Color normal = background({});
    EXPECT_GT(background({.hovered = "b", .focused = {}, .pressed = {}}).r, normal.r);
    EXPECT_GT(background({.hovered = {}, .focused = "b", .pressed = {}}).r, normal.r);
    EXPECT_LT(background({.hovered = "b", .focused = "b", .pressed = "b"}).r, normal.r);
    const HudFrame frame = BuildFrame(doc, 1280, 720, vals);
    ASSERT_EQ(frame.buttons.size(), 1u);
    EXPECT_EQ(frame.buttons[0].action, "go");
    EXPECT_TRUE(frame.items[1].centered);
}

TEST(HudLayout, Image_EmitsAnImageItem)
{
    const HudDocument doc = hudtest::MustParse(
        R"json({"version":1,"widgets":[{"id":"i","type":"image","image":"ui/a.png","size":[8,8]}]})json");
    hudtest::MapValues vals;
    const HudFrame frame = BuildFrame(doc, 1280, 720, vals);
    ASSERT_EQ(frame.items.size(), 1u);
    EXPECT_EQ(frame.items[0].kind, DrawKind::Image);
    EXPECT_EQ(frame.items[0].image, "ui/a.png");
}

TEST(HudLayout, DisplayText_Types)
{
    EXPECT_EQ(DisplayText(values::Value(1.5f)), "1.5");
    EXPECT_EQ(DisplayText(values::Value(std::uint32_t{7})), "7");
    EXPECT_EQ(DisplayText(values::Value(values::EnumValue{"Hard"})), "Hard");
    EXPECT_EQ(DisplayText(values::Value(values::ValueList{.element = values::ValueType::Int, .items = {}})), "0 items");
}
