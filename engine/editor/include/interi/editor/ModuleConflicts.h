// SPDX-License-Identifier: MIT
#pragma once

// The Module Manager's views of a resolved project (no UI): the dependency graph laid out in layers, and every item
// more than one module provides, with the winner, the shadowed providers and the declared `overrides`.

#include <interi/chain/Event.h>
#include <interi/sdk/Hooks.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/values/DataRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

namespace interi::editor
{

enum class GraphEdgeKind : std::uint8_t
{
    Requires, ///< A required dependency.
    Optional, ///< An optional dependency that is present.
    Engine,   ///< A load edge of the Physics Engine selection (declaring module -> engine, engine -> base).
};

struct GraphNodeView
{
    std::string id;
    std::string name;
    sdk::ModuleKind kind = sdk::ModuleKind::Module;
    bool implicit = false;
    std::uint32_t priority = 0;
    /// 0 for modules nothing is below; one more than the deepest module it loads after.
    int layer = 0;
    /// Index within the layer (by id).
    int row = 0;
};

struct GraphEdgeView
{
    std::string from; ///< The module that loads after `to`.
    std::string to;
    GraphEdgeKind kind = GraphEdgeKind::Requires;
};

struct DependencyGraphView
{
    std::vector<GraphNodeView> nodes; ///< Load order.
    std::vector<GraphEdgeView> edges;
    int layerCount = 0;
};

[[nodiscard]] DependencyGraphView BuildDependencyGraph(const sdk::ModuleGraph& graph);

enum class ItemKind : std::uint8_t
{
    Event,
    Function,
    Hook,
    Value,
    Attribute,
    Archetype,
    Table,
};

[[nodiscard]] std::string_view ToString(ItemKind kind) noexcept;

struct ItemProvider
{
    std::string provider;
    std::uint32_t rank = 0;
    bool isOverride = false;
};

/// An item with more than one provider. Project overrides are not providers and are left out.
struct ItemConflict
{
    ItemKind kind = ItemKind::Event;
    std::string id;
    bool shared = false; ///< A shared contract id (no ':'): the highest provider wins outright.
    std::string winner;
    std::vector<ItemProvider> shadowed; ///< By descending priority.
    /// The winner replaced a lower module's item with an explicit `overrides` declaration.
    bool overridden = false;
};

/// An `overrides` entry of a module's manifest and whether it took effect.
struct DeclaredOverrideStatus
{
    std::string owner;
    std::string target;
    /// The module owning `target` isn't in the project.
    bool targetMissing = false;
    /// The owner provides `target` as an override in some registry.
    bool applied = false;
};

struct ConflictReport
{
    /// By kind, then id.
    std::vector<ItemConflict> conflicts;
    std::vector<DeclaredOverrideStatus> overrides;
};

/// Reads the registries a project loaded into. Any registry may be nullptr (a project whose libraries didn't load has
/// no events or hooks; one without data has no Values); `graph` supplies the declared overrides.
[[nodiscard]] ConflictReport CollectConflicts(const sdk::ModuleGraph& graph, const chain::ChainRegistry* chains,
                                              const values::DataRegistry* data, const sdk::HookRegistry* hooks);

} // namespace interi::editor
