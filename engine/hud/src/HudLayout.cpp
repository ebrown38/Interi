// SPDX-License-Identifier: MIT
#include <interi/hud/HudLayout.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace interi::hud
{

namespace
{

float NumericValue(const values::Value* value, bool& ok)
{
    ok = true;
    if (value != nullptr)
    {
        if (const auto* f = std::get_if<float>(&value->Variant()))
        {
            return *f;
        }
        if (const auto* i = std::get_if<std::int32_t>(&value->Variant()))
        {
            return static_cast<float>(*i);
        }
        if (const auto* u = std::get_if<std::uint32_t>(&value->Variant()))
        {
            return static_cast<float>(*u);
        }
    }
    ok = false;
    return 0.0f;
}

bool IsVisible(const HudWidget& widget, const chain::IValueReader& values)
{
    if (widget.visibleIf.empty())
    {
        return true;
    }
    const values::Value* flag = values.Read(widget.visibleIf);
    if (flag != nullptr)
    {
        if (const auto* on = std::get_if<bool>(&flag->Variant()))
        {
            return *on;
        }
    }
    return false;
}

Rect Place(const HudWidget& widget, float scale, float width, float height)
{
    const float w = widget.size[0] * scale;
    const float h = widget.size[1] * scale;
    const auto column = static_cast<int>(widget.anchor) % 3; // 0 left, 1 centre, 2 right
    const auto row = static_cast<int>(widget.anchor) / 3;    // 0 top, 1 middle, 2 bottom
    // Anchors are laid out as TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight.
    const float ax = column == 0 ? 0.0f : column == 1 ? width * 0.5f - w * 0.5f : width - w;
    const float ay = row == 0 ? 0.0f : row == 1 ? height * 0.5f - h * 0.5f : height - h;
    return {ax + widget.offset[0] * scale, ay + widget.offset[1] * scale, w, h};
}

} // namespace

std::string DisplayText(const values::Value& value)
{
    const auto& variant = value.Variant();
    if (const auto* b = std::get_if<bool>(&variant))
    {
        return *b ? "true" : "false";
    }
    if (const auto* u = std::get_if<std::uint32_t>(&variant))
    {
        return std::to_string(*u);
    }
    if (const auto* i = std::get_if<std::int32_t>(&variant))
    {
        return std::to_string(*i);
    }
    if (const auto* f = std::get_if<float>(&variant))
    {
        return std::format("{}", *f);
    }
    if (const auto* s = std::get_if<std::string>(&variant))
    {
        return *s;
    }
    if (const auto* e = std::get_if<values::EnumValue>(&variant))
    {
        return e->name;
    }
    if (const auto* list = std::get_if<values::ValueList>(&variant))
    {
        return std::format("{} items", list->items.size());
    }
    return values::FormatValue(value);
}

std::string FormatHudText(std::string_view text, const chain::IValueReader& values)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{')
        {
            out += '{';
            ++i;
            continue;
        }
        const std::size_t close = text[i] == '{' ? text.find('}', i) : std::string_view::npos;
        if (close == std::string_view::npos)
        {
            out += text[i];
            continue;
        }
        std::string_view name = text.substr(i + 1, close - i - 1);
        int decimals = -1;
        if (const std::size_t bar = name.find('|'); bar != std::string_view::npos)
        {
            const std::string_view digits = name.substr(bar + 1);
            int parsed = 0;
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed);
            if (error == std::errc() && end == digits.data() + digits.size() && parsed >= 0 && parsed <= 9)
            {
                decimals = parsed;
            }
            name = name.substr(0, bar);
        }
        const values::Value* value = values.Read(name);
        bool numeric = false;
        const float number = NumericValue(value, numeric);
        if (value == nullptr)
        {
            out += '?';
        }
        else if (decimals >= 0 && numeric)
        {
            out += std::format("{:.{}f}", number, decimals);
        }
        else
        {
            out += DisplayText(*value);
        }
        i = close;
    }
    return out;
}

HudFrame BuildFrame(const HudDocument& document, float width, float height, const chain::IValueReader& values,
                    const HudHighlight& highlight)
{
    HudFrame frame;
    const float scale = std::min(width / document.referenceSize[0], height / document.referenceSize[1]);
    for (const HudWidget& widget : document.widgets)
    {
        if (!IsVisible(widget, values))
        {
            continue;
        }
        const Rect rect = Place(widget, scale, width, height);
        const auto addBackground = [&](values::Color color)
        {
            if (color.a > 0.0f)
            {
                frame.items.push_back({.kind = DrawKind::Rect,
                                       .rect = rect,
                                       .color = color,
                                       .text = {},
                                       .image = {},
                                       .widget = widget.id,
                                       .centered = false});
            }
        };
        switch (widget.type)
        {
        case WidgetType::Text:
            addBackground(widget.background);
            frame.items.push_back({.kind = DrawKind::Text,
                                   .rect = rect,
                                   .color = widget.color,
                                   .text = FormatHudText(widget.text, values),
                                   .image = {},
                                   .widget = widget.id,
                                   .centered = false});
            break;
        case WidgetType::Bar:
        {
            addBackground(widget.background.a > 0.0f ? widget.background : values::Color{0.0f, 0.0f, 0.0f, 0.5f});
            bool numeric = false;
            const float value = NumericValue(values.Read(widget.bind), numeric);
            const float fraction =
                numeric ? std::clamp((value - widget.min) / (widget.max - widget.min), 0.0f, 1.0f) : 0.0f;
            if (fraction > 0.0f)
            {
                frame.items.push_back({.kind = DrawKind::Rect,
                                       .rect = {rect.x, rect.y, rect.w * fraction, rect.h},
                                       .color = widget.color,
                                       .text = {},
                                       .image = {},
                                       .widget = widget.id,
                                       .centered = false});
            }
            break;
        }
        case WidgetType::Image:
            addBackground(widget.background);
            frame.items.push_back({.kind = DrawKind::Image,
                                   .rect = rect,
                                   .color = widget.color,
                                   .text = {},
                                   .image = widget.image,
                                   .widget = widget.id,
                                   .centered = false});
            break;
        case WidgetType::Button:
        {
            values::Color background =
                widget.background.a > 0.0f ? widget.background : values::Color{0.2f, 0.2f, 0.25f, 0.9f};
            if (highlight.pressed == widget.id)
            {
                background = {background.r * 0.6f, background.g * 0.6f, background.b * 0.6f, background.a};
            }
            else if (highlight.hovered == widget.id || highlight.focused == widget.id)
            {
                background = {std::min(background.r + 0.2f, 1.0f), std::min(background.g + 0.2f, 1.0f),
                              std::min(background.b + 0.2f, 1.0f), background.a};
            }
            addBackground(background);
            frame.items.push_back({.kind = DrawKind::Text,
                                   .rect = rect,
                                   .color = widget.color,
                                   .text = FormatHudText(widget.text, values),
                                   .image = {},
                                   .widget = widget.id,
                                   .centered = true});
            frame.buttons.push_back({.widget = widget.id, .action = widget.action, .rect = rect});
            break;
        }
        case WidgetType::List:
        {
            addBackground(widget.background);
            const values::Value* list = values.Read(widget.bind);
            const auto* items = list == nullptr ? nullptr : std::get_if<values::ValueList>(&list->Variant());
            if (items == nullptr)
            {
                break;
            }
            const float rowHeight = widget.rowHeight * scale;
            const auto rows = static_cast<std::size_t>(std::max(rect.h / rowHeight, 0.0f));
            for (std::size_t row = 0; row < std::min(rows, items->items.size()); ++row)
            {
                frame.items.push_back(
                    {.kind = DrawKind::Text,
                     .rect = {rect.x, rect.y + static_cast<float>(row) * rowHeight, rect.w, rowHeight},
                     .color = widget.color,
                     .text = DisplayText(items->items[row]),
                     .image = {},
                     .widget = widget.id,
                     .centered = false});
            }
            break;
        }
        }
    }
    return frame;
}

} // namespace interi::hud
