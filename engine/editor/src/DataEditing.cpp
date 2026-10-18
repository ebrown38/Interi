// SPDX-License-Identifier: MIT
#include <interi/editor/DataEditing.h>
#include <interi/values/ValueRegistry.h>

#include <format>
#include <functional>
#include <utility>

namespace interi::editor
{

namespace
{

using values::DataRegistry;
using values::Value;

/// An edit of one project-layer item: `apply(value)` stores a value or (nullopt) removes it. Commands with the same
/// item and non-zero merge key merge (a drag is one step).
class SetDataCommand final : public ICommand
{
public:
    using Apply = std::function<void(const std::optional<Value>&)>;

    SetDataCommand(std::string key, std::string label, Apply apply, std::optional<Value> before,
                   std::optional<Value> after, std::uint64_t mergeKey)
        : m_key(std::move(key))
        , m_label(std::move(label))
        , m_apply(std::move(apply))
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeKey(mergeKey)
    {
    }

    void Do() override { m_apply(m_after); }
    void Undo() override { m_apply(m_before); }
    [[nodiscard]] std::string_view Label() const override { return m_label; }

    bool MergeWith(const ICommand& next) override
    {
        const auto* other = dynamic_cast<const SetDataCommand*>(&next);
        if (other == nullptr || m_mergeKey == 0 || other->m_mergeKey != m_mergeKey || other->m_key != m_key)
        {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    std::string m_key;
    std::string m_label;
    Apply m_apply;
    std::optional<Value> m_before;
    std::optional<Value> m_after;
    std::uint64_t m_mergeKey;
};

/// Adds or removes a whole row; undo restores its cells.
class RowCommand final : public ICommand
{
public:
    RowCommand(DataRegistry& data, std::string table, std::string row, std::optional<values::DataRow> before,
               std::optional<values::DataRow> after)
        : m_data(&data)
        , m_table(std::move(table))
        , m_row(std::move(row))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    void Do() override { m_data->Tables().RestoreRow(m_table, m_row, m_after); }
    void Undo() override { m_data->Tables().RestoreRow(m_table, m_row, m_before); }
    [[nodiscard]] std::string_view Label() const override { return m_after ? "Add row" : "Remove row"; }

private:
    DataRegistry* m_data;
    std::string m_table;
    std::string m_row;
    std::optional<values::DataRow> m_before;
    std::optional<values::DataRow> m_after;
};

std::optional<Value> StoredCell(const DataRegistry& data, std::string_view table, std::string_view row,
                                std::string_view column)
{
    const values::DataRow* cells = data.Tables().StoredRow(table, row);
    if (cells == nullptr)
    {
        return std::nullopt;
    }
    const auto it = cells->find(column);
    return it == cells->end() ? std::nullopt : std::optional<Value>(it->second);
}

} // namespace

Result<void> EditValue(DataRegistry& data, UndoStack& undo, std::string_view id, std::optional<Value> value,
                       std::uint64_t mergeKey)
{
    values::ValueRegistry& registry = data.Values();
    const values::ValueDefinition* definition = registry.Definition(id);
    if (definition == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no value '{}'", id));
    }
    if (value)
    {
        if (const Result<void> valid = values::ValidateValue(definition->schema, *value); !valid)
        {
            return MakeError(ErrorCode::InvalidArgument, std::format("'{}': {}", id, valid.error().message));
        }
    }
    const values::ProjectValue* current = registry.GetProjectValue(id);
    std::optional<Value> before = current != nullptr ? std::optional<Value>(current->value) : std::nullopt;
    if (before == value)
    {
        return {};
    }
    auto apply = [&registry, key = std::string(id)](const std::optional<Value>& v)
    {
        if (v)
        {
            registry.LoadProjectValue(key, *v);
        }
        else
        {
            (void)registry.ClearProjectValue(key);
        }
    };
    undo.Execute(std::make_unique<SetDataCommand>(std::string(id), std::format("Set {}", id), std::move(apply),
                                                  std::move(before), std::move(value), mergeKey));
    return {};
}

Result<void> EditAttributeDefault(DataRegistry& data, UndoStack& undo, std::string_view archetype,
                                  std::string_view attribute, std::optional<Value> value, std::uint64_t mergeKey)
{
    values::CharacterRegistry& characters = data.Characters();
    if (value)
    {
        INTERI_TRY(characters.CheckProjectDefault(archetype, attribute, *value));
    }
    const Value* current = characters.ProjectDefault(archetype, attribute);
    std::optional<Value> before = current != nullptr ? std::optional<Value>(*current) : std::nullopt;
    if (before == value)
    {
        return {};
    }
    auto apply = [&characters, a = std::string(archetype), b = std::string(attribute)](const std::optional<Value>& v)
    {
        if (v)
        {
            characters.LoadProjectDefault(a, b, *v);
        }
        else
        {
            (void)characters.ClearProjectDefault(a, b);
        }
    };
    undo.Execute(std::make_unique<SetDataCommand>(std::format("{}/{}", archetype, attribute),
                                                  std::format("Set {} of {}", attribute, archetype), std::move(apply),
                                                  std::move(before), std::move(value), mergeKey));
    return {};
}

Result<void> EditCell(DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row,
                      std::string_view column, std::optional<Value> value, std::uint64_t mergeKey)
{
    values::DataTableRegistry& tables = data.Tables();
    std::optional<Value> before = StoredCell(data, table, row, column);
    if (value)
    {
        // SetCell checks the table, row and column; put the row back so the command applies the change.
        const values::DataRow* stored = tables.StoredRow(table, row);
        std::optional<values::DataRow> original = stored != nullptr ? std::optional(*stored) : std::nullopt;
        INTERI_TRY(tables.SetCell(table, row, column, *value));
        tables.RestoreRow(table, row, std::move(original));
    }
    else if (!tables.HasRow(table, row))
    {
        return MakeError(ErrorCode::NotFound, std::format("table '{}' has no row '{}'", table, row));
    }
    if (before == value)
    {
        return {};
    }
    auto apply =
        [&tables, t = std::string(table), r = std::string(row), c = std::string(column)](const std::optional<Value>& v)
    {
        if (v)
        {
            values::DataRow cells = *tables.StoredRow(t, r);
            cells.insert_or_assign(c, *v);
            tables.LoadRow(t, r, std::move(cells));
        }
        else
        {
            (void)tables.ClearCell(t, r, c);
        }
    };
    undo.Execute(std::make_unique<SetDataCommand>(std::format("{}/{}/{}", table, row, column),
                                                  std::format("Set {}.{}", row, column), std::move(apply),
                                                  std::move(before), std::move(value), mergeKey));
    return {};
}

Result<void> AddTableRow(DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row)
{
    INTERI_TRY(data.Tables().AddRow(table, row)); // checks the id; the command adds it again
    (void)data.Tables().RemoveRow(table, row);
    undo.Execute(
        std::make_unique<RowCommand>(data, std::string(table), std::string(row), std::nullopt, values::DataRow{}));
    return {};
}

Result<void> RemoveTableRow(DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row)
{
    const values::DataRow* cells = data.Tables().StoredRow(table, row);
    if (cells == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("table '{}' has no row '{}'", table, row));
    }
    undo.Execute(std::make_unique<RowCommand>(data, std::string(table), std::string(row), *cells, std::nullopt));
    return {};
}

std::string ProvenanceBadge(std::span<const sdk::ProvenanceEntry> provenance)
{
    return provenance.empty() ? std::string() : provenance.front().provider;
}

std::string ProvenanceTooltip(std::span<const sdk::ProvenanceEntry> provenance)
{
    std::string text;
    for (std::size_t i = 0; i < provenance.size(); ++i)
    {
        if (!text.empty())
        {
            text += '\n';
        }
        text += provenance[i].provider;
        if (i + 1 < provenance.size())
        {
            text +=
                std::format(" ({} {})", provenance[i].isOverride ? "overrides" : "shadows", provenance[i + 1].provider);
        }
    }
    return text;
}

Result<std::unique_ptr<ProjectData>> ProjectData::Create(const sdk::ModuleGraph& graph)
{
    std::unique_ptr<ProjectData> project(new ProjectData());
    project->m_graph = graph;
    project->m_modules = std::make_unique<sdk::ModuleSet>(project->m_graph, std::span<const std::string>{});
    project->m_data = std::make_unique<values::DataRegistry>(*project->m_modules);
    for (const std::size_t index : project->m_graph.loadOrder)
    {
        INTERI_TRY_ASSIGN(content, values::LoadModuleContent(project->m_graph.modules[index].manifest));
        INTERI_TRY(project->m_data->AddModule(content));
    }
    return project;
}

} // namespace interi::editor
