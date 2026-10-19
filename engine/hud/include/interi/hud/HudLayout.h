// SPDX-License-Identifier: MIT
#pragma once

// Turns a HudDocument and the current Values into a draw list. Pure and deterministic; drawing it is the
// renderer's job (editor_ui draws it with ImGui).

#include <interi/chain/Expression.h>
#include <interi/hud/HudDocument.h>
#include <interi/loc/Localization.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::hud
{

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool Contains(float px, float py) const noexcept
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    friend bool operator==(const Rect&, const Rect&) noexcept = default;
};

enum class DrawKind : std::uint8_t
{
    Rect, ///< A filled rectangle.
    Text, ///< `text` inside `rect`, left aligned, vertically centred (Button: centred).
    Image ///< `image` stretched over `rect`.
};

struct DrawItem
{
    DrawKind kind = DrawKind::Rect;
    Rect rect;
    values::Color color;
    std::string text;
    std::string image;
    std::string widget; ///< The widget that produced it.
    bool centered = false;
};

struct ButtonRect
{
    std::string widget;
    std::string action;
    Rect rect;
};

/// Which button is under the pointer or has keyboard focus (widget ids; empty: none). Buttons draw a brighter
/// background for either.
struct HudHighlight
{
    std::string hovered;
    std::string focused;
    std::string pressed;
};

struct HudFrame
{
    std::vector<DrawItem> items;     ///< In draw order (document order, background first).
    std::vector<ButtonRect> buttons; ///< Visible buttons in document order (the focus order).
};

/// The value as UI text: bool true/false, numbers in the shortest form, strings and enums raw, a vec3 "(x, y, z)", a
/// list "N items".
[[nodiscard]] std::string DisplayText(const values::Value& value);

/// Replaces `{id}` and `{id|N}` (N decimals, numbers only) in `text` with the Value; an unknown Value gives "?".
/// `{{` is a literal brace. `{@key}` is the string table entry `key` (Localization::Translate: the key itself when
/// there is no `localization` or no translation); the translation's own `{id}` placeholders are filled from Values,
/// but a `{@key}` inside a translation is left as its key.
[[nodiscard]] std::string FormatHudText(std::string_view text, const chain::IValueReader& values,
                                        const loc::Localization* localization = nullptr);

/// Lays `document` out for a screen of `width` x `height` pixels.
[[nodiscard]] HudFrame BuildFrame(const HudDocument& document, float width, float height,
                                  const chain::IValueReader& values, const HudHighlight& highlight = {},
                                  const loc::Localization* localization = nullptr);

} // namespace interi::hud
