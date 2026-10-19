// SPDX-License-Identifier: MIT
#pragma once

// A HUD or menu as data (`.hud.json`):
//
//     { "version": 1, "name": "hud", "referenceSize": [1280, 720],
//       "widgets": [
//         { "id": "score", "type": "text", "anchor": "topLeft", "offset": [16, 16], "size": [240, 28],
//           "text": "Coins {coins}/{total}", "color": [1, 1, 1, 1] },
//         { "id": "health", "type": "bar", "anchor": "bottomLeft", "offset": [16, -16], "size": [200, 16],
//           "bind": "health", "min": 0, "max": 100 },
//         { "id": "play", "type": "button", "anchor": "center", "size": [200, 40], "text": "Play",
//           "action": "menu.pop" } ] }
//
// Widgets: text (`text`, with `{valueId}` or `{valueId|2}` (decimals) replaced by the Value), bar (`bind`: a numeric
// Value, filled from `min` to `max`), image (`image`: an asset path), button (`text`, `action`) and list (`bind`: a
// List Value, one row each). `visibleIf` names a bool Value that must be true. Positions are in reference pixels,
// scaled uniformly (the smaller ratio) to the screen and placed from the anchor; a negative offset moves
// left/up from the right/bottom anchors. Unknown fields are warnings, anything malformed an error.

#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>
#include <interi/values/Value.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::hud
{

enum class WidgetType : std::uint8_t
{
    Text,
    Bar,
    Image,
    Button,
    List
};

enum class Anchor : std::uint8_t
{
    TopLeft,
    Top,
    TopRight,
    Left,
    Center,
    Right,
    BottomLeft,
    Bottom,
    BottomRight
};

[[nodiscard]] std::string_view ToString(WidgetType type) noexcept;
[[nodiscard]] std::string_view ToString(Anchor anchor) noexcept;

struct HudWidget
{
    std::string id;
    WidgetType type = WidgetType::Text;
    Anchor anchor = Anchor::TopLeft;
    std::array<float, 2> offset{0.0f, 0.0f};
    std::array<float, 2> size{100.0f, 24.0f};
    std::string text;      ///< Text and Button label.
    std::string bind;      ///< Bar: a numeric Value; List: a List Value.
    std::string image;     ///< Image: asset path.
    std::string action;    ///< Button: the action raised on activation.
    std::string visibleIf; ///< A bool Value id; empty: always visible.
    float min = 0.0f;      ///< Bar range.
    float max = 1.0f;
    float rowHeight = 20.0f; ///< List.
    values::Color color{1.0f, 1.0f, 1.0f, 1.0f};
    values::Color background{0.0f, 0.0f, 0.0f, 0.0f};
};

struct HudDocument
{
    std::string name;
    std::array<float, 2> referenceSize{1280.0f, 720.0f};
    std::vector<HudWidget> widgets;

    [[nodiscard]] const HudWidget* Find(std::string_view id) const noexcept;
};

inline constexpr int kHudVersion = 1;

struct ParsedHud
{
    HudDocument document;
    std::vector<std::string> warnings;
};

[[nodiscard]] Result<ParsedHud> ParseHud(std::string_view json);
[[nodiscard]] Result<ParsedHud> LoadHud(const IFileSystem& files, std::string_view path);
/// Canonical text (sorted keys); ParseHud(WriteHud(d)) gives `d` back.
[[nodiscard]] std::string WriteHud(const HudDocument& document);

/// Every Value a document reads (`bind`, `visibleIf`, `{id}` in text), sorted and unique.
[[nodiscard]] std::vector<std::string> BoundValues(const HudDocument& document);
/// The string table keys (`{@key}`) the document's text reads, sorted and unique.
[[nodiscard]] std::vector<std::string> LocalizationKeys(const HudDocument& document);

} // namespace interi::hud
