// SPDX-License-Identifier: MIT
#include <interi/sdk/Manifest.h>
#include <interi/values/DataTables.h>

#include <algorithm>
#include <format>

namespace interi::values
{

namespace
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

} // namespace

const ColumnDefinition* DataTableDefinition::Find(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(columns, name, &ColumnDefinition::name);
    return found == columns.end() ? nullptr : &*found;
}

Result<void> ValidateTableSchema(const DataTableDefinition& table)
{
    for (std::size_t i = 0; i < table.columns.size(); ++i)
    {
        const ColumnDefinition& column = table.columns[i];
        if (!sdk::IsValidItemName(column.name))
        {
            return Invalid(std::format("table '{}': '{}' is not a column name", table.id, column.name));
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (table.columns[j].name == column.name)
            {
                return Invalid(std::format("table '{}': column '{}' appears twice", table.id, column.name));
            }
        }
        for (const std::string& old : column.renamedFrom)
        {
            if (table.Find(old) != nullptr)
            {
                return Invalid(std::format("table '{}': column '{}' is renamed from '{}', which is still a column",
                                           table.id, column.name, old));
            }
        }
        if (const Result<void> valid = ValidateSchema(column.schema); !valid)
        {
            return Invalid(std::format("table '{}', column '{}': {}", table.id, column.name, valid.error().message));
        }
    }
    return {};
}

std::string TableIssue::Text() const
{
    if (column.empty())
    {
        return row.empty() ? std::format("table '{}': {}", table, message)
                           : std::format("table '{}', row '{}': {}", table, row, message);
    }
    return std::format("table '{}', row '{}', column '{}': {}", table, row, column, message);
}

DataTableRegistry::DataTableRegistry(const sdk::ModuleSet& modules)
    : m_tables(modules)
{
}

Result<void> DataTableRegistry::AddTables(std::string_view provider, const std::vector<DataTableDefinition>& tables)
{
    for (const DataTableDefinition& table : tables)
    {
        Result<void> added = ValidateTableSchema(table);
        if (added)
        {
            added = table.isOverride ? m_tables.Override(provider, table.id, table)
                                     : m_tables.Provide(provider, table.id, table);
        }
        if (!added)
        {
            (void)m_tables.ExtractProvider(provider);
            return added;
        }
    }
    return {};
}

std::vector<std::string> DataTableRegistry::RowIds(std::string_view table) const
{
    std::vector<std::string> ids;
    if (const auto it = m_rows.find(table); it != m_rows.end())
    {
        for (const auto& [row, cells] : it->second)
        {
            ids.push_back(row);
        }
    }
    return ids;
}

bool DataTableRegistry::HasRow(std::string_view table, std::string_view row) const noexcept
{
    return StoredRow(table, row) != nullptr;
}

const DataRow* DataTableRegistry::StoredRow(std::string_view table, std::string_view row) const noexcept
{
    const auto it = m_rows.find(table);
    if (it == m_rows.end())
    {
        return nullptr;
    }
    const auto rowIt = it->second.find(row);
    return rowIt == it->second.end() ? nullptr : &rowIt->second;
}

std::optional<Value> DataTableRegistry::Cell(std::string_view table, std::string_view row,
                                             std::string_view column) const
{
    const DataTableDefinition* schema = Table(table);
    const DataRow* cells = StoredRow(table, row);
    const ColumnDefinition* definition = schema == nullptr ? nullptr : schema->Find(column);
    if (definition == nullptr || cells == nullptr)
    {
        return std::nullopt;
    }
    const auto cell = cells->find(column);
    if (cell != cells->end() && ValidateValue(definition->schema, cell->second))
    {
        return cell->second;
    }
    return definition->schema.defaultValue;
}

Result<void> DataTableRegistry::AddRow(std::string_view table, std::string_view row)
{
    if (Table(table) == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no table '{}'", table));
    }
    if (!sdk::IsValidItemName(row) || row.find(':') != std::string_view::npos)
    {
        return Invalid(std::format("table '{}': '{}' is not a row id (letters, digits and \"._-\")", table, row));
    }
    if (HasRow(table, row))
    {
        return Invalid(std::format("table '{}' already has a row '{}'", table, row));
    }
    LoadRow(table, row, {});
    return {};
}

bool DataTableRegistry::RemoveRow(std::string_view table, std::string_view row)
{
    const auto it = m_rows.find(table);
    if (it == m_rows.end())
    {
        return false;
    }
    const auto rowIt = it->second.find(row);
    if (rowIt == it->second.end())
    {
        return false;
    }
    it->second.erase(rowIt);
    if (it->second.empty())
    {
        m_rows.erase(it);
    }
    return true;
}

Result<void> DataTableRegistry::SetCell(std::string_view table, std::string_view row, std::string_view column,
                                        Value value)
{
    const DataTableDefinition* schema = Table(table);
    if (schema == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no table '{}'", table));
    }
    const ColumnDefinition* definition = schema->Find(column);
    if (definition == nullptr)
    {
        return MakeError(ErrorCode::NotFound, TableIssue{.table = std::string(table),
                                                         .row = std::string(row),
                                                         .column = std::string(column),
                                                         .message = "no such column"}
                                                  .Text());
    }
    if (!HasRow(table, row))
    {
        return MakeError(ErrorCode::NotFound, std::format("table '{}' has no row '{}'", table, row));
    }
    if (const Result<void> valid = ValidateValue(definition->schema, value); !valid)
    {
        return Invalid(TableIssue{.table = std::string(table),
                                  .row = std::string(row),
                                  .column = std::string(column),
                                  .message = valid.error().message}
                           .Text());
    }
    m_rows.find(table)->second.find(row)->second.insert_or_assign(std::string(column), std::move(value));
    return {};
}

bool DataTableRegistry::ClearCell(std::string_view table, std::string_view row, std::string_view column)
{
    const auto it = m_rows.find(table);
    if (it == m_rows.end())
    {
        return false;
    }
    const auto rowIt = it->second.find(row);
    if (rowIt == it->second.end())
    {
        return false;
    }
    const auto cell = rowIt->second.find(column);
    if (cell == rowIt->second.end())
    {
        return false;
    }
    rowIt->second.erase(cell);
    return true;
}

void DataTableRegistry::LoadRow(std::string_view table, std::string_view row, DataRow cells)
{
    auto it = m_rows.find(table);
    if (it == m_rows.end())
    {
        it = m_rows.emplace(std::string(table), std::map<std::string, DataRow, std::less<>>{}).first;
    }
    it->second.insert_or_assign(std::string(row), std::move(cells));
}

void DataTableRegistry::RestoreRow(std::string_view table, std::string_view row, std::optional<DataRow> cells)
{
    if (cells)
    {
        LoadRow(table, row, std::move(*cells));
    }
    else
    {
        (void)RemoveRow(table, row);
    }
}

std::vector<TableIssue> DataTableRegistry::ValidateRow(std::string_view table, std::string_view row) const
{
    std::vector<TableIssue> issues;
    const auto issue = [&](std::string_view column, std::string message)
    {
        issues.push_back(TableIssue{.table = std::string(table),
                                    .row = std::string(row),
                                    .column = std::string(column),
                                    .message = std::move(message)});
    };
    const DataTableDefinition* schema = Table(table);
    const DataRow* cells = StoredRow(table, row);
    if (cells == nullptr)
    {
        issue({}, "no such row");
        return issues;
    }
    if (schema == nullptr)
    {
        issue({}, "no module declares this table");
        return issues;
    }
    for (const auto& [column, value] : *cells)
    {
        const ColumnDefinition* definition = schema->Find(column);
        if (definition == nullptr)
        {
            issue(column, "no such column");
        }
        else if (const Result<void> valid = ValidateValue(definition->schema, value); !valid)
        {
            issue(column, valid.error().message);
        }
    }
    return issues;
}

std::vector<TableIssue> DataTableRegistry::Validate() const
{
    std::vector<TableIssue> issues;
    for (const auto& [table, rows] : m_rows)
    {
        for (const auto& [row, cells] : rows)
        {
            std::vector<TableIssue> rowIssues = ValidateRow(table, row);
            issues.insert(issues.end(), rowIssues.begin(), rowIssues.end());
        }
    }
    return issues;
}

} // namespace interi::values
