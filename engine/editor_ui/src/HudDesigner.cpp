// SPDX-License-Identifier: MIT
#include <interi/editor/HudEditing.h>
#include <interi/editor_ui/HudDesigner.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace interi::editor
{

namespace
{

constexpr std::array<hud::WidgetType, 5> kTypes = {hud::WidgetType::Text, hud::WidgetType::Bar, hud::WidgetType::Image,
                                                   hud::WidgetType::Button, hud::WidgetType::List};
constexpr std::array<hud::Anchor, 9> kAnchors = {
    hud::Anchor::TopLeft, hud::Anchor::Top,        hud::Anchor::TopRight, hud::Anchor::Left,       hud::Anchor::Center,
    hud::Anchor::Right,   hud::Anchor::BottomLeft, hud::Anchor::Bottom,   hud::Anchor::BottomRight};

ImU32 ToColor(const values::Color& color)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
}

/// Edits `widget`; true when a field changed this frame. The caller commits it.
bool DrawWidgetFields(hud::HudWidget& widget, bool& released)
{
    bool changed = false;
    const auto track = [&](bool edited)
    {
        changed |= edited;
        released |= ImGui::IsItemDeactivatedAfterEdit();
    };
    track(ImGui::InputText("id", &widget.id));
    int anchor = static_cast<int>(widget.anchor);
    if (ImGui::BeginCombo("anchor", std::string(hud::ToString(widget.anchor)).c_str()))
    {
        for (const hud::Anchor option : kAnchors)
        {
            if (ImGui::Selectable(std::string(hud::ToString(option)).c_str(), option == widget.anchor))
            {
                anchor = static_cast<int>(option);
                widget.anchor = option;
                changed = true;
                released = true;
            }
        }
        ImGui::EndCombo();
    }
    (void)anchor;
    track(ImGui::DragFloat2("offset", widget.offset.data(), 1.0f));
    track(ImGui::DragFloat2("size", widget.size.data(), 1.0f, 0.0f, 10000.0f));
    switch (widget.type)
    {
    case hud::WidgetType::Text:
        track(ImGui::InputText("text", &widget.text));
        break;
    case hud::WidgetType::Bar:
        track(ImGui::InputText("bind", &widget.bind));
        track(ImGui::DragFloat("min", &widget.min));
        track(ImGui::DragFloat("max", &widget.max));
        break;
    case hud::WidgetType::Image:
        track(ImGui::InputText("image", &widget.image));
        break;
    case hud::WidgetType::Button:
        track(ImGui::InputText("text", &widget.text));
        track(ImGui::InputText("action", &widget.action));
        break;
    case hud::WidgetType::List:
        track(ImGui::InputText("bind", &widget.bind));
        track(ImGui::DragFloat("rowHeight", &widget.rowHeight, 0.5f, 1.0f, 500.0f));
        break;
    }
    track(ImGui::InputText("visibleIf", &widget.visibleIf));
    track(ImGui::ColorEdit4("color", &widget.color.r));
    track(ImGui::ColorEdit4("background", &widget.background.r));
    return changed;
}

} // namespace

void DrawHudFrame(ImDrawList& list, ImVec2 origin, const hud::HudFrame& frame)
{
    for (const hud::DrawItem& item : frame.items)
    {
        const ImVec2 low(origin.x + item.rect.x, origin.y + item.rect.y);
        const ImVec2 high(low.x + item.rect.w, low.y + item.rect.h);
        switch (item.kind)
        {
        case hud::DrawKind::Rect:
            list.AddRectFilled(low, high, ToColor(item.color));
            break;
        case hud::DrawKind::Image:
            list.AddRect(low, high, ToColor(item.color));
            list.AddLine(low, high, ToColor(item.color));
            list.AddLine(ImVec2(low.x, high.y), ImVec2(high.x, low.y), ToColor(item.color));
            break;
        case hud::DrawKind::Text:
        {
            const ImVec2 size = ImGui::CalcTextSize(item.text.c_str());
            const ImVec2 at(item.centered ? low.x + (item.rect.w - size.x) * 0.5f : low.x,
                            low.y + (item.rect.h - size.y) * 0.5f);
            list.PushClipRect(low, high, true);
            list.AddText(at, ToColor(item.color), item.text.c_str());
            list.PopClipRect();
            break;
        }
        }
    }
}

HudDesignerResult DrawHudDesignerPanel(hud::HudDocument& document, UndoStack& undo, HudDesignerState& state,
                                       const chain::IValueReader& values, const std::vector<std::string>& knownValues)
{
    HudDesignerResult result;
    if (!ImGui::Begin(kHudDesignerWindow))
    {
        ImGui::End();
        return result;
    }

    ImGui::InputText("name", &document.name);
    for (const hud::WidgetType type : kTypes)
    {
        if (ImGui::SmallButton((std::string("+ ") + std::string(hud::ToString(type))).c_str()))
        {
            state.selected = AddHudWidget(undo, document, type);
            result.edited = true;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::BeginChild("widgets", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f), ImGuiChildFlags_Borders);
    for (const hud::HudWidget& widget : document.widgets)
    {
        const std::string label = widget.id + "  (" + std::string(hud::ToString(widget.type)) + ")";
        if (ImGui::Selectable(label.c_str(), widget.id == state.selected))
        {
            state.selected = widget.id;
        }
    }
    ImGui::EndChild();

    if (const hud::HudWidget* selected = document.Find(state.selected))
    {
        const std::string id = selected->id;
        if (ImGui::SmallButton("Up") && MoveHudWidget(undo, document, id, -1))
        {
            result.edited = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && MoveHudWidget(undo, document, id, 1))
        {
            result.edited = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Duplicate"))
        {
            if (auto copy = DuplicateHudWidget(undo, document, id))
            {
                state.selected = *copy;
                result.edited = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove") && RemoveHudWidget(undo, document, id))
        {
            state.selected.clear();
            result.edited = true;
        }
    }
    if (const hud::HudWidget* selected = document.Find(state.selected))
    {
        hud::HudWidget edited = *selected;
        const std::string oldId = selected->id;
        bool released = false;
        if (DrawWidgetFields(edited, released))
        {
            if (auto done = EditHudWidget(undo, document, oldId, edited))
            {
                state.selected = edited.id;
                state.message.clear();
                result.edited = true;
            }
            else
            {
                state.message = done.error().message;
            }
        }
        if (released)
        {
            undo.Seal();
        }
    }
    if (!state.message.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", state.message.c_str());
    }

    for (const std::string& id : hud::BoundValues(document))
    {
        if (std::ranges::find(knownValues, id) == knownValues.end())
        {
            result.unknownValues.push_back(id);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Unknown Value: %s", id.c_str());
        }
    }

    ImGui::DragFloat("preview width", &state.previewWidth, 1.0f, 64.0f, 8192.0f);
    ImGui::DragFloat("preview height", &state.previewHeight, 1.0f, 64.0f, 8192.0f);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float fit = std::min(available.x / state.previewWidth, 1.0f);
    const float width = state.previewWidth * fit;
    const float height = state.previewHeight * fit;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(24, 24, 28, 255));
    list.PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);
    DrawHudFrame(list, origin, hud::BuildFrame(document, width, height, values));
    list.PopClipRect();

    ImGui::End();
    return result;
}

} // namespace interi::editor
