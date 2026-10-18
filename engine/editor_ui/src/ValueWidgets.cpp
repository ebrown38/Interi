// SPDX-License-Identifier: MIT
#include <interi/editor/EditorMath.h>
#include <interi/editor_ui/ValueWidgets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <limits>
#include <string>

namespace interi::editor
{

namespace
{

using values::Value;
using values::ValueSchema;
using values::ValueType;

[[nodiscard]] float DragSpeed(const ValueSchema& schema, float fallback) noexcept
{
    return schema.meta.step > 0.0 ? static_cast<float>(schema.meta.step) : fallback;
}

[[nodiscard]] std::string FloatFormat(const ValueSchema& schema)
{
    return schema.meta.unit.empty() ? std::string("%.3f") : std::format("%.3f {}", schema.meta.unit);
}

template <typename T>
[[nodiscard]] T Bound(const std::optional<double>& bound, T fallback) noexcept
{
    if (!bound)
    {
        return fallback;
    }
    if constexpr (std::is_floating_point_v<T>)
    {
        return static_cast<T>(*bound);
    }
    else
    {
        const double clamped = std::clamp(*bound, static_cast<double>(std::numeric_limits<T>::min()),
                                          static_cast<double>(std::numeric_limits<T>::max()));
        return static_cast<T>(clamped);
    }
}

/// A text field whose value is returned when the field is left after an edit.
[[nodiscard]] std::optional<std::string> TextField(const char* label, const std::string& text)
{
    std::string edited = text;
    (void)ImGui::InputText(label, &edited);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        return edited;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Value> ScalarWidget(const char* label, const ValueSchema& schema, const Value& value)
{
    switch (schema.type)
    {
    case ValueType::Bool:
    {
        bool edited = std::get<bool>(value);
        if (ImGui::Checkbox(label, &edited))
        {
            return edited;
        }
        break;
    }
    case ValueType::U32:
    {
        std::uint32_t edited = std::get<std::uint32_t>(value);
        const auto min = Bound<std::uint32_t>(schema.meta.min, 0u);
        const auto max = Bound<std::uint32_t>(schema.meta.max, std::numeric_limits<std::uint32_t>::max());
        if (ImGui::DragScalar(label, ImGuiDataType_U32, &edited, DragSpeed(schema, 1.0f), &min, &max))
        {
            return edited;
        }
        break;
    }
    case ValueType::Int:
    {
        std::int32_t edited = std::get<std::int32_t>(value);
        const auto min = Bound<std::int32_t>(schema.meta.min, std::numeric_limits<std::int32_t>::min());
        const auto max = Bound<std::int32_t>(schema.meta.max, std::numeric_limits<std::int32_t>::max());
        if (ImGui::DragScalar(label, ImGuiDataType_S32, &edited, DragSpeed(schema, 1.0f), &min, &max))
        {
            return edited;
        }
        break;
    }
    case ValueType::Float:
    {
        float edited = std::get<float>(value);
        if (ImGui::DragFloat(label, &edited, DragSpeed(schema, 0.05f), Bound(schema.meta.min, 0.0f),
                             Bound(schema.meta.max, 0.0f), FloatFormat(schema).c_str()))
        {
            return edited;
        }
        break;
    }
    case ValueType::Vec3:
    {
        Vec3 edited = std::get<Vec3>(value);
        if (ImGui::DragFloat3(label, &edited.x, DragSpeed(schema, 0.05f), Bound(schema.meta.min, 0.0f),
                              Bound(schema.meta.max, 0.0f), FloatFormat(schema).c_str()))
        {
            return edited;
        }
        break;
    }
    case ValueType::Quat:
    {
        Vec3 degrees = EulerDegrees(std::get<Quat>(value));
        if (ImGui::DragFloat3(label, &degrees.x, 0.5f))
        {
            return FromEulerDegrees(degrees);
        }
        break;
    }
    case ValueType::Color:
    {
        values::Color edited = std::get<values::Color>(value);
        if (ImGui::ColorEdit4(label, &edited.r, ImGuiColorEditFlags_Float))
        {
            return edited;
        }
        break;
    }
    case ValueType::String:
        if (auto edited = TextField(label, std::get<std::string>(value)))
        {
            return Value(std::move(*edited));
        }
        break;
    case ValueType::Enum:
    {
        const std::string& current = std::get<values::EnumValue>(value).name;
        if (schema.options.empty())
        {
            if (auto edited = TextField(label, current))
            {
                return values::EnumValue{.name = std::move(*edited)};
            }
            break;
        }
        std::optional<Value> result;
        if (ImGui::BeginCombo(label, current.c_str()))
        {
            for (const std::string& option : schema.options)
            {
                if (ImGui::Selectable(option.c_str(), option == current) && option != current)
                {
                    result = values::EnumValue{.name = option};
                }
            }
            ImGui::EndCombo();
        }
        return result;
    }
    case ValueType::AssetRef:
        if (auto edited = TextField(label, std::get<values::AssetRef>(value).path))
        {
            return values::AssetRef{.path = std::move(*edited)};
        }
        break;
    case ValueType::EntityRef:
    {
        std::uint64_t edited = std::get<values::EntityRef>(value).id;
        if (ImGui::DragScalar(label, ImGuiDataType_U64, &edited, 1.0f))
        {
            return values::EntityRef{.id = edited};
        }
        break;
    }
    case ValueType::List:
        break;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Value> ListWidget(const char* label, const ValueSchema& schema, const Value& value)
{
    const auto& list = std::get<values::ValueList>(value);
    std::optional<Value> result;
    if (!ImGui::TreeNode(label, "%s %s", label, values::FormatValue(value).c_str()))
    {
        return result;
    }
    ValueSchema element = schema;
    element.type = schema.element;
    for (std::size_t i = 0; i < list.items.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const std::string itemLabel = std::format("[{}]", i);
        if (auto edited = ScalarWidget(itemLabel.c_str(), element, list.items[i]))
        {
            values::ValueList changed = list;
            changed.items[i] = std::move(*edited);
            result = std::move(changed);
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+"))
    {
        values::ValueList changed = list;
        changed.items.push_back(values::ZeroValue(schema.element));
        result = std::move(changed);
    }
    ImGui::SameLine();
    if (!list.items.empty() && ImGui::SmallButton("-"))
    {
        values::ValueList changed = list;
        changed.items.pop_back();
        result = std::move(changed);
    }
    ImGui::TreePop();
    return result;
}

} // namespace

std::optional<Value> ValueWidget(const char* label, const ValueSchema& schema, const Value& value)
{
    if (values::TypeOf(value) != schema.type)
    {
        const std::string expected(values::ToString(schema.type));
        ImGui::Text("%s: %s (expected %s)", label, values::FormatValue(value).c_str(), expected.c_str());
        return std::nullopt;
    }
    ImGui::BeginDisabled(schema.meta.readOnly);
    std::optional<Value> edited =
        schema.type == ValueType::List ? ListWidget(label, schema, value) : ScalarWidget(label, schema, value);
    ImGui::EndDisabled();
    if (!schema.meta.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", schema.meta.tooltip.c_str());
    }
    return schema.meta.readOnly ? std::nullopt : edited;
}

} // namespace interi::editor
