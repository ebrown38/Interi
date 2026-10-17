// SPDX-License-Identifier: MIT
#pragma once

// Data tables: a module declares a table's schema (its columns are Value schemas), the project supplies the rows,
// keyed by row id. module.json:
//
//     "dataTables": [
//       { "id": "Guns", "version": 2, "columns": [
//           { "name": "range", "type": "float", "default": 50, "min": 0, "max": 1000, "unit": "m",
//             "renamedFrom": ["reach"] },
//           { "name": "mode", "type": "enum", "options": ["Single", "Auto"] } ] }
//     ]
//
// A table is a registry item like a Value ("overrides" replaces a lower module's table schema whole). A row holds
// cells by column name; a missing cell reads as the column's default, a cell that doesn't fit its column (wrong type,
// out of range) reads as the default too and is reported by Validate with its table, row and column.

#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::values
{

struct ColumnDefinition
{
    std::string name;
    ValueSchema schema;
    std::vector<std::string> renamedFrom; ///< Earlier names: project cells under them migrate to this column.
};

struct DataTableDefinition
{
    std::string id;
    std::vector<ColumnDefinition> columns;
    std::uint32_t version = 1;
    bool isOverride = false;

    /// The column named `name`, or nullptr.
    [[nodiscard]] const ColumnDefinition* Find(std::string_view name) const noexcept;
};

/// Checks a table schema: valid unique column names, no column renamed from another column's name.
[[nodiscard]] Result<void> ValidateTableSchema(const DataTableDefinition& table);

/// One row's cells by column name.
using DataRow = std::map<std::string, Value, std::less<>>;

/// A problem with the project's rows, located by table, row and column.
struct TableIssue
{
    std::string table;
    std::string row;
    std::string column; ///< Empty for a problem with the whole row or table.
    std::string message;

    /// "table 'com.a:Guns', row 'shotgun', column 'range': 120 is above the maximum 100".
    [[nodiscard]] std::string Text() const;
};

/// Table schemas of a project's modules plus the project's rows. Not thread-safe to modify.
class DataTableRegistry
{
public:
    using Entries = std::vector<std::pair<std::string, sdk::ContributionRegistry<DataTableDefinition>::Entry>>;
    /// table -> row -> cells.
    using Rows = std::map<std::string, std::map<std::string, DataRow, std::less<>>, std::less<>>;

    explicit DataTableRegistry(const sdk::ModuleSet& modules);

    DataTableRegistry(const DataTableRegistry&) = delete;
    DataTableRegistry& operator=(const DataTableRegistry&) = delete;
    DataTableRegistry(DataTableRegistry&&) = delete;
    DataTableRegistry& operator=(DataTableRegistry&&) = delete;
    ~DataTableRegistry() = default;

    /// Adds every table `provider` declares (all or nothing).
    Result<void> AddTables(std::string_view provider, const std::vector<DataTableDefinition>& tables);
    [[nodiscard]] Entries ExtractProvider(std::string_view provider) { return m_tables.ExtractProvider(provider); }
    void Restore(Entries entries) { m_tables.Restore(std::move(entries)); }

    /// The winning schema, or nullptr.
    [[nodiscard]] const DataTableDefinition* Table(std::string_view id) const noexcept { return m_tables.Resolve(id); }
    [[nodiscard]] std::vector<std::string> TableIds() const { return m_tables.Ids(); }
    [[nodiscard]] std::vector<sdk::ProvenanceEntry> Provenance(std::string_view id) const
    {
        return m_tables.Provenance(id);
    }

    /// The project's row ids of a table, sorted (also for a table no module declares any more).
    [[nodiscard]] std::vector<std::string> RowIds(std::string_view table) const;
    [[nodiscard]] bool HasRow(std::string_view table, std::string_view row) const noexcept;
    /// The stored cells of a row, or nullptr.
    [[nodiscard]] const DataRow* StoredRow(std::string_view table, std::string_view row) const noexcept;
    [[nodiscard]] const Rows& ProjectRows() const noexcept { return m_rows; }

    /// The effective cell: the stored value if it fits the column, else the column's default. nullopt for an unknown
    /// table, row or column.
    [[nodiscard]] std::optional<Value> Cell(std::string_view table, std::string_view row,
                                            std::string_view column) const;
    template <typename T>
    [[nodiscard]] Result<T> Get(std::string_view table, std::string_view row, std::string_view column) const
    {
        const std::optional<Value> value = Cell(table, row, column);
        if (!value)
        {
            return MakeError(
                ErrorCode::NotFound,
                std::string("no cell '").append(table).append("/").append(row).append("/").append(column).append("'"));
        }
        const T* typed = std::get_if<T>(&value->Variant());
        if (typed == nullptr)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::string("cell '").append(column).append("' is a ").append(ToString(TypeOf(*value))));
        }
        return *typed;
    }

    /// Adds an empty row (every cell at its default). InvalidArgument for an invalid or existing row id, NotFound
    /// for an unknown table.
    Result<void> AddRow(std::string_view table, std::string_view row);
    bool RemoveRow(std::string_view table, std::string_view row);
    /// Sets a cell, checked against its column (the message names the table, row and column).
    Result<void> SetCell(std::string_view table, std::string_view row, std::string_view column, Value value);
    /// Removes a stored cell (it reads as the default again). False if it wasn't stored.
    bool ClearCell(std::string_view table, std::string_view row, std::string_view column);
    /// Stores a row without checking it (loading a project); Validate reports what doesn't fit.
    void LoadRow(std::string_view table, std::string_view row, DataRow cells);
    /// Replaces a row's stored cells wholesale (undo); nullopt removes the row.
    void RestoreRow(std::string_view table, std::string_view row, std::optional<DataRow> cells);

    /// Removes every row of every table.
    void ClearRows() noexcept { m_rows.clear(); }

    /// Checks one row: unknown table or column, cells that don't fit.
    [[nodiscard]] std::vector<TableIssue> ValidateRow(std::string_view table, std::string_view row) const;
    /// Checks every stored row of every table.
    [[nodiscard]] std::vector<TableIssue> Validate() const;

private:
    sdk::ContributionRegistry<DataTableDefinition> m_tables;
    Rows m_rows;
};

} // namespace interi::values
