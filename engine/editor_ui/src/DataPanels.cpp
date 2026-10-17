// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/editor/DataEditing.h>
#include <interi/editor_ui/DataPanels.h>
#include <interi/editor_ui/ValueWidgets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>
#include <map>
#include <string_view>
#include <vector>

namespace interi::editor
{

namespace
{

/// Undo merge key for the widget just drawn.
[[nodiscard]] std::uint64_t MergeKey(std::string_view item) noexcept
{
    return Hash64(item, std::uint64_t{ImGui::GetItemID()}) | 1u;
}

/// The part of an id after the module ("com.test.a:JumpHeight" -> "JumpHeight").
[[nodiscard]] std::string_view ShortName(std::string_view id) noexcept
{
    const std::size_t colon = id.find(':');
    return colon == std::string_view::npos ? id : id.substr(colon + 1);
}

/// "[provider]" in a colour by kind (project / module), with the full provenance as its tooltip.
void Badge(std::span<const sdk::ProvenanceEntry> provenance)
{
    const std::string badge = ProvenanceBadge(provenance);
    if (badge.empty())
    {
        return;
    }
    const bool project = badge == values::kProjectProviderId;
    ImGui::TextColored(project ? ImVec4(0.95f, 0.75f, 0.3f, 1.0f) : ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "[%s]",
                       badge.c_str());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", ProvenanceTooltip(provenance).c_str());
    }
    ImGui::SameLine();
}

/// Records the result of an edit: a rejection is shown in the panel.
void Report(const Result<void>& result, DataPanelState& state)
{
    state.message = result ? std::string() : result.error().message;
}

void Message(const DataPanelState& state)
{
    if (!state.message.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.message.c_str());
    }
}

/// A combo over `ids`; keeps `selected` valid (the first id when it isn't one).
void SelectId(const char* label, const std::vector<std::string>& ids, std::string& selected)
{
    if (std::ranges::find(ids, selected) == ids.end())
    {
        selected = ids.empty() ? std::string() : ids.front();
    }
    if (ImGui::BeginCombo(label, selected.c_str()))
    {
        for (const std::string& id : ids)
        {
            if (ImGui::Selectable(id.c_str(), id == selected))
            {
                selected = id;
            }
        }
        ImGui::EndCombo();
    }
}

} // namespace

void DrawValuesPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state)
{
    if (ImGui::Begin(kValuesWindow))
    {
        values::ValueRegistry& registry = data.Values();
        std::map<std::string, std::vector<std::string>> byCategory;
        for (std::string& id : registry.Ids())
        {
            const std::string& category = registry.Definition(id)->schema.meta.category;
            byCategory[category.empty() ? std::string("General") : category].push_back(std::move(id));
        }
        if (byCategory.empty())
        {
            ImGui::TextUnformatted("The project's modules declare no Values.");
        }
        Message(state);
        for (const auto& [category, ids] : byCategory)
        {
            if (!ImGui::CollapsingHeader(category.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                continue;
            }
            for (const std::string& id : ids)
            {
                ImGui::PushID(id.c_str());
                const values::ValueDefinition& definition = *registry.Definition(id);
                Badge(registry.Provenance(id));
                const std::string label(ShortName(id));
                if (auto edited = ValueWidget(label.c_str(), definition.schema, *registry.Get(id)))
                {
                    Report(EditValue(data, undo, id, std::move(edited), MergeKey(id)), state);
                }
                if (ImGui::IsItemDeactivated())
                {
                    undo.Seal();
                }
                if (const values::ProjectValue* project = registry.GetProjectValue(id))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset"))
                    {
                        Report(EditValue(data, undo, id, std::nullopt), state);
                    }
                    if (!project->active)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "  project value %s doesn't fit; unused",
                                           values::FormatValue(project->value).c_str());
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void DrawCharactersPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state)
{
    if (ImGui::Begin(kCharactersWindow))
    {
        values::CharacterRegistry& characters = data.Characters();
        const std::vector<std::string> archetypes = characters.ArchetypeIds();
        if (archetypes.empty())
        {
            ImGui::TextUnformatted("The project's modules declare no archetypes.");
        }
        else
        {
            SelectId("Archetype", archetypes, state.archetype);
            Badge(characters.ArchetypeProvenance(state.archetype));
            ImGui::NewLine();
            Message(state);
            const Result<values::AttributeLayout> layout = characters.Layout(state.archetype);
            if (!layout)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", layout.error().message.c_str());
            }
            else
            {
                for (const values::AttributeSlotInfo& slot : layout->slots)
                {
                    ImGui::PushID(slot.id.c_str());
                    const bool overridden = characters.ProjectDefault(state.archetype, slot.id) != nullptr;
                    std::vector<sdk::ProvenanceEntry> provenance = characters.AttributeProvenance(slot.id);
                    if (overridden)
                    {
                        provenance.insert(provenance.begin(), {.provider = std::string(values::kProjectProviderId),
                                                               .rank = 0xFFFFFFFFu,
                                                               .isOverride = true});
                    }
                    Badge(provenance);
                    const std::string label(ShortName(slot.id));
                    if (auto edited = ValueWidget(label.c_str(), slot.schema, slot.schema.defaultValue))
                    {
                        Report(EditAttributeDefault(data, undo, state.archetype, slot.id, std::move(edited),
                                                    MergeKey(state.archetype + slot.id)),
                               state);
                    }
                    if (ImGui::IsItemDeactivated())
                    {
                        undo.Seal();
                    }
                    if (overridden)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Reset"))
                        {
                            Report(EditAttributeDefault(data, undo, state.archetype, slot.id, std::nullopt), state);
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::End();
}

void DrawDataTablesPanel(values::DataRegistry& data, UndoStack& undo, DataPanelState& state)
{
    if (ImGui::Begin(kDataTablesWindow))
    {
        values::DataTableRegistry& tables = data.Tables();
        const std::vector<std::string> ids = tables.TableIds();
        if (ids.empty())
        {
            ImGui::TextUnformatted("The project's modules declare no data tables.");
        }
        else
        {
            SelectId("Table", ids, state.table);
            Badge(tables.Provenance(state.table));
            ImGui::NewLine();
            Message(state);
            const values::DataTableDefinition& table = *tables.Table(state.table);
            for (const values::TableIssue& issue : tables.Validate())
            {
                if (issue.table == state.table)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", issue.Text().c_str());
                }
            }
            const int columns = static_cast<int>(table.columns.size()) + 1;
            if (ImGui::BeginTable("rows", columns,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollX))
            {
                ImGui::TableSetupColumn("row");
                for (const values::ColumnDefinition& column : table.columns)
                {
                    ImGui::TableSetupColumn(column.name.c_str());
                }
                ImGui::TableHeadersRow();
                std::string removed;
                for (const std::string& row : tables.RowIds(state.table))
                {
                    ImGui::PushID(row.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("x"))
                    {
                        removed = row;
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(row.c_str());
                    for (const values::ColumnDefinition& column : table.columns)
                    {
                        ImGui::TableNextColumn();
                        ImGui::PushID(column.name.c_str());
                        ImGui::SetNextItemWidth(-1.0f);
                        const values::Value cell =
                            tables.Cell(state.table, row, column.name).value_or(column.schema.defaultValue);
                        if (auto edited = ValueWidget("##cell", column.schema, cell))
                        {
                            Report(EditCell(data, undo, state.table, row, column.name, std::move(edited),
                                            MergeKey(state.table + row + column.name)),
                                   state);
                        }
                        if (ImGui::IsItemDeactivated())
                        {
                            undo.Seal();
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (!removed.empty())
                {
                    Report(RemoveTableRow(data, undo, state.table, removed), state);
                }
            }
            ImGui::InputText("##newRow", &state.newRow);
            ImGui::SameLine();
            if (ImGui::Button("Add row"))
            {
                const Result<void> added = AddTableRow(data, undo, state.table, state.newRow);
                Report(added, state);
                if (added)
                {
                    state.newRow.clear();
                }
            }
        }
    }
    ImGui::End();
}

} // namespace interi::editor
