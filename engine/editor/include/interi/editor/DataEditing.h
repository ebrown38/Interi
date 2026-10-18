// SPDX-License-Identifier: MIT
#pragma once

// Undoable edits of the project's data (Values, archetype attribute defaults, data table rows and cells), plus the
// provenance badges the panels show. Every edit is checked against the winning definition before it is recorded;
// edits with the same non-zero merge key merge until the undo stack is sealed (a drag is one step).

#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/values/DataRegistry.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// Sets the project's value of a Value (`value`), or removes it (nullopt: back to the module default).
Result<void> EditValue(values::DataRegistry& data, UndoStack& undo, std::string_view id,
                       std::optional<values::Value> value, std::uint64_t mergeKey = 0);

/// Sets (or, with nullopt, removes) the project's default of one attribute of an archetype.
Result<void> EditAttributeDefault(values::DataRegistry& data, UndoStack& undo, std::string_view archetype,
                                  std::string_view attribute, std::optional<values::Value> value,
                                  std::uint64_t mergeKey = 0);

/// Sets (or, with nullopt, clears) one cell of a row.
Result<void> EditCell(values::DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row,
                      std::string_view column, std::optional<values::Value> value, std::uint64_t mergeKey = 0);

/// Adds a row (every cell at its default) or removes one, undoably.
Result<void> AddTableRow(values::DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row);
Result<void> RemoveTableRow(values::DataRegistry& data, UndoStack& undo, std::string_view table, std::string_view row);

/// The provider shown on a badge: who set the effective item ("project", or the winning module id). Empty if nobody
/// provides it.
[[nodiscard]] std::string ProvenanceBadge(std::span<const sdk::ProvenanceEntry> provenance);

/// One line per provider, winner first: "project (overrides com.test.b)", "com.test.b (shadows com.test.a)".
[[nodiscard]] std::string ProvenanceTooltip(std::span<const sdk::ProvenanceEntry> provenance);

/// A project's data for the editor: the module set and DataRegistry of a resolved module graph, built from the
/// manifests' data sections (no libraries are opened).
class ProjectData
{
public:
    /// Builds the registry of `graph` (all or nothing).
    [[nodiscard]] static Result<std::unique_ptr<ProjectData>> Create(const sdk::ModuleGraph& graph);

    [[nodiscard]] values::DataRegistry& Data() noexcept { return *m_data; }
    [[nodiscard]] const values::DataRegistry& Data() const noexcept { return *m_data; }
    [[nodiscard]] const sdk::ModuleGraph& Graph() const noexcept { return m_graph; }

private:
    ProjectData() = default;

    sdk::ModuleGraph m_graph;
    std::unique_ptr<sdk::ModuleSet> m_modules;
    std::unique_ptr<values::DataRegistry> m_data;
};

} // namespace interi::editor
