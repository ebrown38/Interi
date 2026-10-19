// SPDX-License-Identifier: MIT
#include <interi/editor/ModuleConflicts.h>

#include <algorithm>
#include <map>

namespace interi::editor
{

namespace
{

struct Item
{
    ItemKind kind;
    std::string id;
    std::vector<sdk::ProvenanceEntry> providers; ///< Winner first, project overrides removed.
};

void Add(std::vector<Item>& items, ItemKind kind, std::vector<std::string> ids, const auto& provenance)
{
    for (std::string& id : ids)
    {
        std::vector<sdk::ProvenanceEntry> providers = provenance(id);
        std::erase_if(providers,
                      [](const sdk::ProvenanceEntry& entry) { return entry.provider == values::kProjectProviderId; });
        items.push_back({.kind = kind, .id = std::move(id), .providers = std::move(providers)});
    }
}

} // namespace

DependencyGraphView BuildDependencyGraph(const sdk::ModuleGraph& graph)
{
    DependencyGraphView view;
    std::map<std::string, int, std::less<>> layers;
    for (const std::size_t index : graph.loadOrder)
    {
        const sdk::ResolvedModule& module = graph.modules[index];
        int layer = 0;
        for (const std::string& below : module.loadAfter)
        {
            if (const auto it = layers.find(below); it != layers.end())
            {
                layer = std::max(layer, it->second + 1);
            }
            const auto dependency = std::ranges::find(module.manifest.dependencies, below, &sdk::ModuleDependency::id);
            GraphEdgeKind kind = GraphEdgeKind::Engine;
            if (dependency != module.manifest.dependencies.end())
            {
                kind = dependency->optional ? GraphEdgeKind::Optional : GraphEdgeKind::Requires;
            }
            view.edges.push_back({.from = module.manifest.id, .to = below, .kind = kind});
        }
        layers[module.manifest.id] = layer;
        view.layerCount = std::max(view.layerCount, layer + 1);
        view.nodes.push_back({.id = module.manifest.id,
                              .name = module.manifest.name,
                              .kind = module.manifest.kind,
                              .implicit = module.implicit,
                              .priority = module.priority,
                              .layer = layer,
                              .row = 0});
    }
    std::vector<GraphNodeView*> order;
    order.reserve(view.nodes.size());
    for (GraphNodeView& node : view.nodes)
    {
        order.push_back(&node);
    }
    std::ranges::sort(order, [](const GraphNodeView* a, const GraphNodeView* b)
                      { return a->layer != b->layer ? a->layer < b->layer : a->id < b->id; });
    int row = 0;
    int previousLayer = -1;
    for (GraphNodeView* node : order)
    {
        row = node->layer == previousLayer ? row + 1 : 0;
        previousLayer = node->layer;
        node->row = row;
    }
    return view;
}

std::string_view ToString(ItemKind kind) noexcept
{
    switch (kind)
    {
    case ItemKind::Event:
        return "Event";
    case ItemKind::Function:
        return "Function";
    case ItemKind::Hook:
        return "Hook";
    case ItemKind::Value:
        return "Value";
    case ItemKind::Attribute:
        return "Attribute";
    case ItemKind::Archetype:
        return "Archetype";
    case ItemKind::Table:
        return "Table";
    }
    return "Event";
}

ConflictReport CollectConflicts(const sdk::ModuleGraph& graph, const chain::ChainRegistry* chains,
                                const values::DataRegistry* data, const sdk::HookRegistry* hooks)
{
    std::vector<Item> items;
    if (chains != nullptr)
    {
        const auto& events = chains->Events().Registry();
        Add(items, ItemKind::Event, events.Ids(), [&](const std::string& id) { return events.Provenance(id); });
        const auto& functions = chains->Functions().Registry();
        Add(items, ItemKind::Function, functions.Ids(),
            [&](const std::string& id) { return functions.Provenance(id); });
    }
    if (hooks != nullptr)
    {
        const auto& registry = hooks->Registry();
        Add(items, ItemKind::Hook, registry.Ids(), [&](const std::string& id) { return registry.Provenance(id); });
    }
    if (data != nullptr)
    {
        const values::ValueRegistry& valueRegistry = data->Values();
        Add(items, ItemKind::Value, valueRegistry.Ids(),
            [&](const std::string& id) { return valueRegistry.Provenance(id); });
        const values::CharacterRegistry& characters = data->Characters();
        Add(items, ItemKind::Attribute, characters.AttributeIds(),
            [&](const std::string& id) { return characters.AttributeProvenance(id); });
        Add(items, ItemKind::Archetype, characters.ArchetypeIds(),
            [&](const std::string& id) { return characters.ArchetypeProvenance(id); });
        const values::DataTableRegistry& tables = data->Tables();
        Add(items, ItemKind::Table, tables.TableIds(), [&](const std::string& id) { return tables.Provenance(id); });
    }

    ConflictReport report;
    for (const Item& item : items)
    {
        if (item.providers.size() < 2)
        {
            continue;
        }
        ItemConflict conflict;
        conflict.kind = item.kind;
        conflict.id = item.id;
        conflict.shared = !sdk::IsLocalId(item.id);
        conflict.winner = item.providers.front().provider;
        conflict.overridden = item.providers.front().isOverride;
        for (std::size_t i = 1; i < item.providers.size(); ++i)
        {
            const sdk::ProvenanceEntry& entry = item.providers[i];
            conflict.shadowed.push_back(
                {.provider = entry.provider, .rank = entry.rank, .isOverride = entry.isOverride});
        }
        report.conflicts.push_back(std::move(conflict));
    }
    std::ranges::sort(report.conflicts, [](const ItemConflict& a, const ItemConflict& b)
                      { return a.kind != b.kind ? a.kind < b.kind : a.id < b.id; });

    for (const sdk::ResolvedModule& module : graph.modules)
    {
        for (const sdk::OverrideDeclaration& declaration : module.manifest.overrides)
        {
            DeclaredOverrideStatus status;
            status.owner = module.manifest.id;
            status.target = declaration.target;
            status.targetMissing = graph.Get(sdk::OwnerOf(declaration.target)) == nullptr;
            status.applied = std::ranges::any_of(
                items,
                [&](const Item& item)
                {
                    return item.id == declaration.target &&
                           std::ranges::any_of(item.providers, [&](const auto& entry)
                                               { return entry.isOverride && entry.provider == module.manifest.id; });
                });
            report.overrides.push_back(std::move(status));
        }
    }
    return report;
}

} // namespace interi::editor
