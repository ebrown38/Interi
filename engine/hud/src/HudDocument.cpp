// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/hud/HudDocument.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace interi::hud
{

namespace
{

using Json = nlohmann::json;

constexpr std::array<std::string_view, 5> kTypeNames = {"text", "bar", "image", "button", "list"};
constexpr std::array<std::string_view, 9> kAnchorNames = {"topLeft", "top",        "topRight", "left",       "center",
                                                          "right",   "bottomLeft", "bottom",   "bottomRight"};

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsNumber(const Json& value)
{
    return value.is_number() && std::isfinite(value.get<double>());
}

Result<std::array<float, 2>> ReadPair(const Json& value, std::string_view widget, std::string_view field)
{
    if (!value.is_array() || value.size() != 2 || !IsNumber(value[0]) || !IsNumber(value[1]))
    {
        return Invalid(std::format("widget '{}': \"{}\" must be [x, y]", widget, field));
    }
    return std::array<float, 2>{value[0].get<float>(), value[1].get<float>()};
}

Result<values::Color> ReadColor(const Json& value, std::string_view widget, std::string_view field)
{
    if (!value.is_array() || value.size() != 4 || !std::ranges::all_of(value, IsNumber))
    {
        return Invalid(std::format("widget '{}': \"{}\" must be [r, g, b, a]", widget, field));
    }
    return values::Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

Result<std::string> ReadString(const Json& value, std::string_view widget, std::string_view field)
{
    if (!value.is_string())
    {
        return Invalid(std::format("widget '{}': \"{}\" must be a string", widget, field));
    }
    return value.get<std::string>();
}

Result<float> ReadNumber(const Json& value, std::string_view widget, std::string_view field)
{
    if (!IsNumber(value))
    {
        return Invalid(std::format("widget '{}': \"{}\" must be a finite number", widget, field));
    }
    return value.get<float>();
}

Result<HudWidget> ParseWidget(const Json& entry, std::vector<std::string>& warnings)
{
    if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
        entry["id"].get<std::string>().empty())
    {
        return Invalid("every widget needs a non-empty \"id\"");
    }
    HudWidget widget;
    widget.id = entry["id"].get<std::string>();
    const std::string& id = widget.id;
    const auto type = entry.find("type");
    if (type == entry.end() || !type->is_string())
    {
        return Invalid(std::format("widget '{}': needs a \"type\"", id));
    }
    const auto typeIt = std::ranges::find(kTypeNames, type->get<std::string>());
    if (typeIt == kTypeNames.end())
    {
        return Invalid(std::format("widget '{}': unknown type \"{}\"", id, type->get<std::string>()));
    }
    widget.type = static_cast<WidgetType>(typeIt - kTypeNames.begin());
    for (const auto& [field, value] : entry.items())
    {
        if (field == "id" || field == "type")
        {
            continue;
        }
        if (field == "anchor")
        {
            INTERI_TRY_ASSIGN(name, ReadString(value, id, field));
            const auto anchor = std::ranges::find(kAnchorNames, name);
            if (anchor == kAnchorNames.end())
            {
                return Invalid(std::format("widget '{}': unknown anchor \"{}\"", id, name));
            }
            widget.anchor = static_cast<Anchor>(anchor - kAnchorNames.begin());
        }
        else if (field == "offset")
        {
            INTERI_TRY_ASSIGN(pair, ReadPair(value, id, field));
            widget.offset = pair;
        }
        else if (field == "size")
        {
            INTERI_TRY_ASSIGN(pair, ReadPair(value, id, field));
            if (pair[0] < 0.0f || pair[1] < 0.0f)
            {
                return Invalid(std::format("widget '{}': \"size\" must not be negative", id));
            }
            widget.size = pair;
        }
        else if (field == "text" || field == "bind" || field == "image" || field == "action" || field == "visibleIf")
        {
            INTERI_TRY_ASSIGN(text, ReadString(value, id, field));
            (field == "text"     ? widget.text
             : field == "bind"   ? widget.bind
             : field == "image"  ? widget.image
             : field == "action" ? widget.action
                                 : widget.visibleIf) = std::move(text);
        }
        else if (field == "min" || field == "max" || field == "rowHeight")
        {
            INTERI_TRY_ASSIGN(number, ReadNumber(value, id, field));
            (field == "min" ? widget.min : field == "max" ? widget.max : widget.rowHeight) = number;
        }
        else if (field == "color" || field == "background")
        {
            INTERI_TRY_ASSIGN(color, ReadColor(value, id, field));
            (field == "color" ? widget.color : widget.background) = color;
        }
        else
        {
            warnings.push_back(std::format("widget '{}': unknown field \"{}\" ignored", id, field));
        }
    }
    if (widget.rowHeight <= 0.0f)
    {
        return Invalid(std::format("widget '{}': \"rowHeight\" must be positive", id));
    }
    if (widget.type == WidgetType::Bar && (widget.bind.empty() || widget.max <= widget.min))
    {
        return Invalid(std::format("widget '{}': a bar needs \"bind\" and max > min", id));
    }
    if (widget.type == WidgetType::List && widget.bind.empty())
    {
        return Invalid(std::format("widget '{}': a list needs \"bind\"", id));
    }
    if (widget.type == WidgetType::Button && widget.action.empty())
    {
        return Invalid(std::format("widget '{}': a button needs an \"action\"", id));
    }
    if (widget.type == WidgetType::Image && widget.image.empty())
    {
        return Invalid(std::format("widget '{}': an image needs \"image\"", id));
    }
    return widget;
}

Json PairJson(const std::array<float, 2>& pair)
{
    return Json::array({pair[0], pair[1]});
}

Json ColorJson(const values::Color& color)
{
    return Json::array({color.r, color.g, color.b, color.a});
}

/// Calls `visit(name)` for each `{id}` / `{id|N}` / `{@key}` in `text` (a key arrives with its '@').
template <typename F>
void ForEachPlaceholder(std::string_view text, F&& visit)
{
    for (std::size_t at = text.find('{'); at != std::string_view::npos; at = text.find('{', at))
    {
        const std::size_t close = text.find('}', at);
        if (close == std::string_view::npos)
        {
            return;
        }
        std::string_view name = text.substr(at + 1, close - at - 1);
        if (!name.starts_with('@'))
        {
            name = name.substr(0, name.find('|'));
        }
        if (!name.empty())
        {
            visit(name);
        }
        at = close + 1;
    }
}

} // namespace

std::string_view ToString(WidgetType type) noexcept
{
    return kTypeNames[static_cast<std::size_t>(type)];
}

std::string_view ToString(Anchor anchor) noexcept
{
    return kAnchorNames[static_cast<std::size_t>(anchor)];
}

const HudWidget* HudDocument::Find(std::string_view id) const noexcept
{
    const auto found = std::ranges::find(widgets, id, &HudWidget::id);
    return found == widgets.end() ? nullptr : &*found;
}

Result<ParsedHud> ParseHud(std::string_view json)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("HUD document is not a JSON object");
    }
    ParsedHud parsed;
    bool versioned = false;
    for (const auto& [key, value] : root.items())
    {
        if (key == "version")
        {
            if (!value.is_number_integer() || value.get<int>() != kHudVersion)
            {
                return MakeError(ErrorCode::VersionMismatch, "unsupported HUD document version");
            }
            versioned = true;
        }
        else if (key == "name")
        {
            if (!value.is_string())
            {
                return Invalid("\"name\" must be a string");
            }
            parsed.document.name = value.get<std::string>();
        }
        else if (key == "referenceSize")
        {
            if (!value.is_array() || value.size() != 2 || !IsNumber(value[0]) || !IsNumber(value[1]) ||
                value[0].get<float>() <= 0.0f || value[1].get<float>() <= 0.0f)
            {
                return Invalid("\"referenceSize\" must be two positive numbers");
            }
            parsed.document.referenceSize = {value[0].get<float>(), value[1].get<float>()};
        }
        else if (key == "widgets")
        {
            if (!value.is_array())
            {
                return Invalid("\"widgets\" must be an array");
            }
            std::set<std::string> seen;
            for (const Json& entry : value)
            {
                INTERI_TRY_ASSIGN(widget, ParseWidget(entry, parsed.warnings));
                if (!seen.insert(widget.id).second)
                {
                    return Invalid(std::format("duplicate widget id '{}'", widget.id));
                }
                parsed.document.widgets.push_back(std::move(widget));
            }
        }
        else
        {
            parsed.warnings.push_back(std::format("HUD: unknown field \"{}\" ignored", key));
        }
    }
    if (!versioned)
    {
        return Invalid("HUD document has no \"version\"");
    }
    for (const std::string& warning : parsed.warnings)
    {
        INTERI_LOG_WARN("Hud", "{}", warning);
    }
    return parsed;
}

Result<ParsedHud> LoadHud(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto parsed = ParseHud(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed)
    {
        return MakeError(parsed.error().code, std::format("{}: {}", path, parsed.error().message));
    }
    return parsed;
}

std::string WriteHud(const HudDocument& document)
{
    Json root = Json::object();
    root["version"] = kHudVersion;
    root["name"] = document.name;
    root["referenceSize"] = PairJson(document.referenceSize);
    Json widgets = Json::array();
    for (const HudWidget& widget : document.widgets)
    {
        Json entry = Json::object();
        entry["id"] = widget.id;
        entry["type"] = std::string(ToString(widget.type));
        entry["anchor"] = std::string(ToString(widget.anchor));
        entry["offset"] = PairJson(widget.offset);
        entry["size"] = PairJson(widget.size);
        entry["color"] = ColorJson(widget.color);
        entry["background"] = ColorJson(widget.background);
        const std::pair<const char*, const std::string*> strings[] = {{"text", &widget.text},
                                                                      {"bind", &widget.bind},
                                                                      {"image", &widget.image},
                                                                      {"action", &widget.action},
                                                                      {"visibleIf", &widget.visibleIf}};
        for (const auto& [name, text] : strings)
        {
            if (!text->empty())
            {
                entry[name] = *text;
            }
        }
        if (widget.type == WidgetType::Bar)
        {
            entry["min"] = widget.min;
            entry["max"] = widget.max;
        }
        if (widget.type == WidgetType::List)
        {
            entry["rowHeight"] = widget.rowHeight;
        }
        widgets.push_back(std::move(entry));
    }
    root["widgets"] = std::move(widgets);
    return root.dump(2);
}

std::vector<std::string> BoundValues(const HudDocument& document)
{
    std::set<std::string> ids;
    for (const HudWidget& widget : document.widgets)
    {
        if (!widget.bind.empty())
        {
            ids.insert(widget.bind);
        }
        if (!widget.visibleIf.empty())
        {
            ids.insert(widget.visibleIf);
        }
        ForEachPlaceholder(widget.text,
                           [&](std::string_view id)
                           {
                               if (!id.starts_with('@'))
                               {
                                   ids.emplace(id);
                               }
                           });
    }
    return {ids.begin(), ids.end()};
}

std::vector<std::string> LocalizationKeys(const HudDocument& document)
{
    std::set<std::string> keys;
    for (const HudWidget& widget : document.widgets)
    {
        ForEachPlaceholder(widget.text,
                           [&](std::string_view name)
                           {
                               if (name.starts_with('@') && name.size() > 1)
                               {
                                   keys.emplace(name.substr(1));
                               }
                           });
    }
    return {keys.begin(), keys.end()};
}

} // namespace interi::hud
