// SPDX-License-Identifier: MIT
#include <interi/editor/ChainEditing.h>

#include <interi/chain/CoreLibrary.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace interi::editor
{

namespace
{

using chain::ChainAsset;
using chain::ChainLink;
using chain::ChainNode;

constexpr std::string_view kProbe = "__probe";

/// Sets `*asset` to `after` and back. `mergeKey` (non-empty) lets consecutive edits of one thing merge.
class ChainSnapshotCommand final : public ICommand
{
public:
    ChainSnapshotCommand(ChainAsset* asset, ChainAsset after, std::string label, std::string mergeKey)
        : m_asset(asset)
        , m_before(*asset)
        , m_after(std::move(after))
        , m_label(std::move(label))
        , m_mergeKey(std::move(mergeKey))
    {
    }

    void Do() override { *m_asset = m_after; }
    void Undo() override { *m_asset = m_before; }
    [[nodiscard]] std::string_view Label() const override { return m_label; }

    bool MergeWith(const ICommand& next) override
    {
        const auto* other = dynamic_cast<const ChainSnapshotCommand*>(&next);
        if (other == nullptr || m_mergeKey.empty() || other->m_mergeKey != m_mergeKey || other->m_asset != m_asset)
        {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    ChainAsset* m_asset;
    ChainAsset m_before;
    ChainAsset m_after;
    std::string m_label;
    std::string m_mergeKey;
};

void Apply(UndoStack& undo, ChainAsset& asset, ChainAsset after, std::string label, std::string mergeKey = {})
{
    undo.Execute(std::make_unique<ChainSnapshotCommand>(&asset, std::move(after), std::move(label), std::move(mergeKey)));
}

void ApplySealed(UndoStack& undo, ChainAsset& asset, ChainAsset after, std::string label)
{
    Apply(undo, asset, std::move(after), std::move(label));
    undo.Seal();
}

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

std::unexpected<Error> NoNode(std::string_view id)
{
    return MakeError(ErrorCode::NotFound, std::format("no node '{}'", id));
}

bool IsName(std::string_view name)
{
    return !name.empty() &&
           std::ranges::all_of(
               name, [](char c)
               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

ChainNode* FindNode(ChainAsset& asset, std::string_view id)
{
    const auto found = std::ranges::find(asset.nodes, id, &ChainNode::id);
    return found == asset.nodes.end() ? nullptr : &*found;
}

const ChainNode* FindNode(const ChainAsset& asset, std::string_view id)
{
    const auto found = std::ranges::find(asset.nodes, id, &ChainNode::id);
    return found == asset.nodes.end() ? nullptr : &*found;
}

const chain::EventDefinition* Resolve(const ChainEditContext& context, std::string_view event)
{
    return context.registry == nullptr ? nullptr : context.registry->Events().Resolve(event);
}

std::string Stem(std::string_view event)
{
    const std::size_t cut = event.find_last_of(".:");
    std::string stem(cut == std::string_view::npos ? event : event.substr(cut + 1));
    std::erase_if(stem, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '_'; });
    return stem.empty() ? "node" : stem;
}

template <typename Taken>
std::string UniqueName(std::string_view stem, const Taken& taken)
{
    for (int n = 1;; ++n)
    {
        std::string candidate = std::format("{}{}", stem, n);
        if (!taken(candidate))
        {
            return candidate;
        }
    }
}

std::string UniqueNodeId(const ChainAsset& asset, std::string_view stem)
{
    return UniqueName(stem, [&](const std::string& id) { return FindNode(asset, id) != nullptr; });
}

/// Compiles `asset`; with `probe`, every node nothing links to hangs off a Start trigger so its inputs are checked.
std::vector<chain::Diagnostic> CompileDiagnostics(const ChainAsset& asset, const ChainEditContext& context, bool probe)
{
    if (context.registry == nullptr)
    {
        return {};
    }
    ChainAsset copy = asset;
    if (probe && context.registry->Events().Resolve(chain::kTriggerStart) != nullptr)
    {
        copy.nodes.push_back({.id = std::string(kProbe), .event = std::string(chain::kTriggerStart), .params = {}});
        for (const ChainNode& node : asset.nodes)
        {
            const chain::EventDefinition* definition = Resolve(context, node.event);
            const bool linked = std::ranges::any_of(asset.links, [&](const ChainLink& l) { return l.toNode == node.id; });
            if (!linked && definition != nullptr && definition->kind == chain::EventKind::Action)
            {
                copy.links.push_back({.fromNode = std::string(kProbe), .fromOutput = {}, .toNode = node.id, .condition = {}});
            }
        }
    }
    auto result = chain::CompileChain(copy, *context.registry, context.values);
    std::erase_if(result.diagnostics, [](const chain::Diagnostic& d) { return d.node == kProbe; });
    return std::move(result.diagnostics);
}

/// Errors in `after` that `before` didn't have (by text).
std::optional<std::string> IntroducedError(const std::vector<chain::Diagnostic>& before,
                                           const std::vector<chain::Diagnostic>& after)
{
    std::multiset<std::string> known;
    for (const chain::Diagnostic& d : before)
    {
        if (d.severity == chain::Diagnostic::Severity::Error)
        {
            known.insert(d.Text());
        }
    }
    for (const chain::Diagnostic& d : after)
    {
        if (d.severity != chain::Diagnostic::Severity::Error)
        {
            continue;
        }
        const std::string text = d.Text();
        if (const auto it = known.find(text); it != known.end())
        {
            known.erase(it);
            continue;
        }
        const std::string where = d.input.empty() ? d.node : std::format("{}.{}", d.node, d.input);
        return where.empty() ? d.message : std::format("{}: {}", where, d.message);
    }
    return std::nullopt;
}

/// Refuses `after` when it adds a compile error relative to `before`.
Result<void> CheckEdit(const ChainAsset& before, const ChainAsset& after, const ChainEditContext& context)
{
    if (context.registry == nullptr)
    {
        return {};
    }
    if (auto error = IntroducedError(CompileDiagnostics(before, context, true), CompileDiagnostics(after, context, true)))
    {
        return Invalid(*error);
    }
    return {};
}

} // namespace

std::vector<chain::Diagnostic> ValidateChain(const ChainAsset& asset, const ChainEditContext& context)
{
    return CompileDiagnostics(asset, context, false);
}

std::vector<const chain::Diagnostic*> DiagnosticsForNode(const std::vector<chain::Diagnostic>& all, std::string_view id)
{
    std::vector<const chain::Diagnostic*> result;
    for (const chain::Diagnostic& d : all)
    {
        if (d.node == id)
        {
            result.push_back(&d);
        }
    }
    return result;
}

std::vector<const chain::Diagnostic*> DiagnosticsForLink(const std::vector<chain::Diagnostic>& all, std::size_t index)
{
    std::vector<const chain::Diagnostic*> result;
    for (const chain::Diagnostic& d : all)
    {
        if (d.link == index)
        {
            result.push_back(&d);
        }
    }
    return result;
}

bool HasErrors(const std::vector<chain::Diagnostic>& diagnostics) noexcept
{
    return std::ranges::any_of(diagnostics,
                               [](const chain::Diagnostic& d) { return d.severity == chain::Diagnostic::Severity::Error; });
}

// ---- Palette -----------------------------------------------------------------------------------------------------

std::vector<PaletteEntry> BuildPalette(const chain::ChainRegistry& registry)
{
    const auto& events = registry.Events().Registry();
    std::vector<PaletteEntry> entries;
    for (const std::string& id : events.Ids())
    {
        const chain::EventDefinition* definition = events.Resolve(id);
        if (definition == nullptr)
        {
            continue;
        }
        const auto provenance = events.Provenance(id);
        PaletteEntry entry;
        entry.id = id;
        entry.module = provenance.empty() ? "engine" : provenance.front().provider;
        entry.category = definition->category;
        entry.tooltip = definition->tooltip;
        entry.kind = definition->kind;
        for (std::size_t i = 1; i < provenance.size(); ++i)
        {
            entry.shadowed.push_back(provenance[i].provider);
        }
        if (!entry.shadowed.empty())
        {
            std::string others;
            for (std::size_t i = 0; i < entry.shadowed.size(); ++i)
            {
                others += i == 0 ? "" : (i + 1 == entry.shadowed.size() ? " and " : ", ");
                others += entry.shadowed[i];
            }
            entry.indicator = std::format("provided by {}, overrides {}", entry.module, others);
        }
        entries.push_back(std::move(entry));
    }
    std::ranges::sort(entries,
                      [](const PaletteEntry& a, const PaletteEntry& b)
                      { return std::tie(a.module, a.category, a.id) < std::tie(b.module, b.category, b.id); });
    return entries;
}

std::vector<const PaletteEntry*> FilterPalette(const std::vector<PaletteEntry>& palette, std::string_view query)
{
    const auto lower = [](std::string_view text)
    {
        std::string result(text);
        std::ranges::transform(result, result.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return result;
    };
    std::vector<std::string> words;
    {
        const std::string text = lower(query);
        std::size_t begin = 0;
        while (begin < text.size())
        {
            const std::size_t end = text.find(' ', begin);
            const std::size_t stop = end == std::string::npos ? text.size() : end;
            if (stop > begin)
            {
                words.push_back(text.substr(begin, stop - begin));
            }
            begin = stop + 1;
        }
    }
    std::vector<const PaletteEntry*> result;
    for (const PaletteEntry& entry : palette)
    {
        const std::string haystack = lower(entry.id + " " + entry.module + " " + entry.category + " " + entry.tooltip);
        if (std::ranges::all_of(words, [&](const std::string& w) { return haystack.find(w) != std::string::npos; }))
        {
            result.push_back(&entry);
        }
    }
    return result;
}

// ---- Editing -----------------------------------------------------------------------------------------------------

Result<std::string> AddChainNode(UndoStack& undo, ChainAsset& asset, const ChainEditContext& context,
                                 std::string_view event, std::array<float, 2> pos)
{
    if (context.registry != nullptr && Resolve(context, event) == nullptr)
    {
        return Invalid(std::format("no event '{}'", event));
    }
    ChainAsset after = asset;
    ChainNode node;
    node.id = UniqueNodeId(asset, Stem(event));
    node.event = std::string(event);
    node.pos = pos;
    std::string id = node.id;
    after.nodes.push_back(std::move(node));
    ApplySealed(undo, asset, std::move(after), std::format("Add {}", id));
    return id;
}

Result<void> RemoveChainNodes(UndoStack& undo, ChainAsset& asset, const std::vector<std::string>& ids)
{
    for (const std::string& id : ids)
    {
        if (FindNode(asset, id) == nullptr)
        {
            return NoNode(id);
        }
    }
    if (ids.empty())
    {
        return {};
    }
    const auto gone = [&](const std::string& id) { return std::ranges::contains(ids, id); };
    ChainAsset after = asset;
    std::erase_if(after.nodes, [&](const ChainNode& n) { return gone(n.id); });
    std::erase_if(after.links, [&](const ChainLink& l) { return gone(l.fromNode) || gone(l.toNode); });
    for (chain::ChainGroup& group : after.groups)
    {
        std::erase_if(group.nodes, gone);
    }
    std::erase_if(after.groups, [](const chain::ChainGroup& g) { return g.nodes.empty(); });
    ApplySealed(undo, asset, std::move(after), ids.size() == 1 ? std::format("Remove {}", ids[0]) : "Remove nodes");
    return {};
}

Result<void> RenameChainNode(UndoStack& undo, ChainAsset& asset, std::string_view id, std::string_view name)
{
    if (FindNode(asset, id) == nullptr)
    {
        return NoNode(id);
    }
    if (id == name)
    {
        return {};
    }
    if (!IsName(name))
    {
        return Invalid(std::format("'{}' is not a name ([A-Za-z0-9_])", name));
    }
    if (FindNode(asset, name) != nullptr || name == kProbe)
    {
        return Invalid(std::format("node '{}' already exists", name));
    }
    ChainAsset after = asset;
    FindNode(after, id)->id = std::string(name);
    for (std::size_t i = 0; i < after.links.size(); ++i)
    {
        if (asset.links[i].fromNode == id)
        {
            after.links[i].fromNode = std::string(name);
        }
        if (asset.links[i].toNode == id)
        {
            after.links[i].toNode = std::string(name);
        }
    }
    for (chain::ChainGroup& group : after.groups)
    {
        std::ranges::replace(group.nodes, std::string(id), std::string(name));
    }
    ApplySealed(undo, asset, std::move(after), std::format("Rename {}", id));
    return {};
}

Result<void> MoveChainNodes(UndoStack& undo, ChainAsset& asset, const std::vector<std::string>& ids,
                            std::array<float, 2> delta, const std::vector<std::string>& comments)
{
    for (const std::string& id : ids)
    {
        if (FindNode(asset, id) == nullptr)
        {
            return NoNode(id);
        }
    }
    ChainAsset after = asset;
    std::string key = "move";
    for (const std::string& id : ids)
    {
        FindNode(after, id)->pos[0] += delta[0];
        FindNode(after, id)->pos[1] += delta[1];
        key += ":" + id;
    }
    for (const std::string& id : comments)
    {
        const auto comment = std::ranges::find(after.comments, id, &chain::ChainComment::id);
        if (comment == after.comments.end())
        {
            return MakeError(ErrorCode::NotFound, std::format("no comment '{}'", id));
        }
        comment->pos[0] += delta[0];
        comment->pos[1] += delta[1];
        key += "#" + id;
    }
    Apply(undo, asset, std::move(after), "Move", std::move(key));
    return {};
}

Result<void> AddChainLink(UndoStack& undo, ChainAsset& asset, const ChainEditContext& context, std::string_view fromNode,
                          std::string_view fromOutput, std::string_view toNode, std::string_view condition)
{
    const ChainNode* from = FindNode(asset, fromNode);
    const ChainNode* to = FindNode(asset, toNode);
    if (from == nullptr)
    {
        return NoNode(fromNode);
    }
    if (to == nullptr)
    {
        return NoNode(toNode);
    }
    if (fromNode == toNode)
    {
        return Invalid("a node can't link to itself");
    }
    if (context.registry != nullptr)
    {
        const chain::EventDefinition* source = Resolve(context, from->event);
        const chain::EventDefinition* target = Resolve(context, to->event);
        if (source == nullptr || target == nullptr)
        {
            return Invalid(std::format("unknown event '{}'", source == nullptr ? from->event : to->event));
        }
        if (source->outputs.empty())
        {
            return Invalid(std::format("'{}' has no outputs", from->event));
        }
        if (!fromOutput.empty() && source->FindOutput(fromOutput) == nullptr)
        {
            return Invalid(std::format("'{}' has no output '{}'", from->event, fromOutput));
        }
        if (target->kind == chain::EventKind::Trigger)
        {
            return Invalid(std::format("'{}' is a trigger: nothing can link to it", to->event));
        }
    }
    const auto same = [&](const ChainLink& l)
    { return l.fromNode == fromNode && l.fromOutput == fromOutput && l.toNode == toNode; };
    if (std::ranges::any_of(asset.links, same))
    {
        return Invalid("that link exists already");
    }
    ChainAsset after = asset;
    after.links.push_back({.fromNode = std::string(fromNode),
                           .fromOutput = std::string(fromOutput),
                           .toNode = std::string(toNode),
                           .condition = std::string(condition)});
    INTERI_TRY(CheckEdit(asset, after, context));
    ApplySealed(undo, asset, std::move(after), std::format("Link {} to {}", fromNode, toNode));
    return {};
}

Result<void> RemoveChainLink(UndoStack& undo, ChainAsset& asset, std::size_t index)
{
    if (index >= asset.links.size())
    {
        return MakeError(ErrorCode::NotFound, std::format("no link {}", index));
    }
    ChainAsset after = asset;
    after.links.erase(after.links.begin() + static_cast<std::ptrdiff_t>(index));
    ApplySealed(undo, asset, std::move(after), "Remove link");
    return {};
}

Result<void> SetChainLinkCondition(UndoStack& undo, ChainAsset& asset, const ChainEditContext& context,
                                   std::size_t index, std::string_view condition)
{
    if (index >= asset.links.size())
    {
        return MakeError(ErrorCode::NotFound, std::format("no link {}", index));
    }
    if (asset.links[index].condition == condition)
    {
        return {};
    }
    ChainAsset after = asset;
    after.links[index].condition = std::string(condition);
    INTERI_TRY(CheckEdit(asset, after, context));
    Apply(undo, asset, std::move(after), "Edit condition", std::format("cond:{}", index));
    return {};
}

Result<void> BindChainInput(UndoStack& undo, ChainAsset& asset, const ChainEditContext& context, std::string_view node,
                            std::string_view input, std::optional<chain::Binding> binding)
{
    const ChainNode* target = FindNode(asset, node);
    if (target == nullptr)
    {
        return NoNode(node);
    }
    if (const chain::EventDefinition* definition = Resolve(context, target->event);
        definition != nullptr && definition->InputIndex(input) == definition->inputs.size())
    {
        return Invalid(std::format("'{}' has no input '{}'", target->event, input));
    }
    ChainAsset after = asset;
    ChainNode& edited = *FindNode(after, node);
    std::erase_if(edited.params, [&](const auto& p) { return p.first == input; });
    if (binding)
    {
        edited.params.emplace_back(std::string(input), std::move(*binding));
        std::ranges::sort(edited.params, {}, &std::pair<std::string, chain::Binding>::first);
    }
    if (after == asset)
    {
        return {};
    }
    INTERI_TRY(CheckEdit(asset, after, context));
    Apply(undo, asset, std::move(after), std::format("Bind {}.{}", node, input), std::format("bind:{}.{}", node, input));
    return {};
}

Result<void> SetChainPriority(UndoStack& undo, ChainAsset& asset, std::int32_t priority)
{
    if (asset.priority == priority)
    {
        return {};
    }
    ChainAsset after = asset;
    after.priority = priority;
    Apply(undo, asset, std::move(after), "Set priority", "priority");
    return {};
}

Result<std::string> AddChainComment(UndoStack& undo, ChainAsset& asset, std::array<float, 2> pos, std::string text)
{
    ChainAsset after = asset;
    chain::ChainComment comment;
    comment.id = UniqueName("comment", [&](const std::string& id)
                            { return std::ranges::contains(asset.comments, id, &chain::ChainComment::id); });
    comment.text = std::move(text);
    comment.pos = pos;
    std::string id = comment.id;
    after.comments.push_back(std::move(comment));
    ApplySealed(undo, asset, std::move(after), "Add comment");
    return id;
}

Result<void> EditChainComment(UndoStack& undo, ChainAsset& asset, std::string_view id, chain::ChainComment edited)
{
    const auto found = std::ranges::find(asset.comments, id, &chain::ChainComment::id);
    if (found == asset.comments.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("no comment '{}'", id));
    }
    edited.id = std::string(id);
    if (edited == *found)
    {
        return {};
    }
    ChainAsset after = asset;
    after.comments[static_cast<std::size_t>(found - asset.comments.begin())] = std::move(edited);
    Apply(undo, asset, std::move(after), "Edit comment", std::format("comment:{}", id));
    return {};
}

Result<void> RemoveChainComment(UndoStack& undo, ChainAsset& asset, std::string_view id)
{
    if (!std::ranges::contains(asset.comments, id, &chain::ChainComment::id))
    {
        return MakeError(ErrorCode::NotFound, std::format("no comment '{}'", id));
    }
    ChainAsset after = asset;
    std::erase_if(after.comments, [&](const chain::ChainComment& c) { return c.id == id; });
    ApplySealed(undo, asset, std::move(after), "Remove comment");
    return {};
}

Result<std::string> GroupChainNodes(UndoStack& undo, ChainAsset& asset, const std::vector<std::string>& nodes,
                                    std::string title)
{
    if (nodes.empty())
    {
        return Invalid("nothing to group");
    }
    for (const std::string& id : nodes)
    {
        if (FindNode(asset, id) == nullptr)
        {
            return NoNode(id);
        }
    }
    ChainAsset after = asset;
    for (chain::ChainGroup& group : after.groups)
    {
        std::erase_if(group.nodes, [&](const std::string& id) { return std::ranges::contains(nodes, id); });
    }
    std::erase_if(after.groups, [](const chain::ChainGroup& g) { return g.nodes.empty(); });
    chain::ChainGroup group;
    group.id = UniqueName("group", [&](const std::string& id)
                          { return std::ranges::contains(after.groups, id, &chain::ChainGroup::id); });
    group.title = std::move(title);
    group.nodes = nodes;
    std::string id = group.id;
    after.groups.push_back(std::move(group));
    ApplySealed(undo, asset, std::move(after), "Group");
    return id;
}

Result<void> EditChainGroup(UndoStack& undo, ChainAsset& asset, std::string_view id, chain::ChainGroup edited)
{
    const auto found = std::ranges::find(asset.groups, id, &chain::ChainGroup::id);
    if (found == asset.groups.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("no group '{}'", id));
    }
    edited.id = std::string(id);
    for (const std::string& node : edited.nodes)
    {
        if (FindNode(asset, node) == nullptr)
        {
            return NoNode(node);
        }
    }
    if (edited == *found)
    {
        return {};
    }
    ChainAsset after = asset;
    after.groups[static_cast<std::size_t>(found - asset.groups.begin())] = std::move(edited);
    Apply(undo, asset, std::move(after), "Edit group", std::format("group:{}", id));
    return {};
}

Result<void> UngroupChain(UndoStack& undo, ChainAsset& asset, std::string_view id)
{
    if (!std::ranges::contains(asset.groups, id, &chain::ChainGroup::id))
    {
        return MakeError(ErrorCode::NotFound, std::format("no group '{}'", id));
    }
    ChainAsset after = asset;
    std::erase_if(after.groups, [&](const chain::ChainGroup& g) { return g.id == id; });
    ApplySealed(undo, asset, std::move(after), "Ungroup");
    return {};
}

ChainClipboard CopyChainNodes(const ChainAsset& asset, const std::vector<std::string>& ids)
{
    ChainClipboard clipboard;
    for (const ChainNode& node : asset.nodes)
    {
        if (std::ranges::contains(ids, node.id))
        {
            clipboard.nodes.push_back(node);
        }
    }
    for (const ChainLink& link : asset.links)
    {
        if (std::ranges::contains(ids, link.fromNode) && std::ranges::contains(ids, link.toNode))
        {
            clipboard.links.push_back(link);
        }
    }
    return clipboard;
}

Result<std::vector<std::string>> PasteChainNodes(UndoStack& undo, ChainAsset& asset, const ChainClipboard& clipboard,
                                                 std::array<float, 2> offset)
{
    if (clipboard.Empty())
    {
        return std::vector<std::string>{};
    }
    ChainAsset after = asset;
    std::map<std::string, std::string> renamed;
    std::vector<std::string> created;
    for (const ChainNode& source : clipboard.nodes)
    {
        ChainNode node = source;
        node.id = UniqueNodeId(after, Stem(source.event));
        node.pos = {source.pos[0] + offset[0], source.pos[1] + offset[1]};
        renamed[source.id] = node.id;
        created.push_back(node.id);
        after.nodes.push_back(std::move(node));
    }
    for (ChainLink link : clipboard.links)
    {
        link.fromNode = renamed.at(link.fromNode);
        link.toNode = renamed.at(link.toNode);
        after.links.push_back(std::move(link));
    }
    ApplySealed(undo, asset, std::move(after), "Paste");
    return created;
}

} // namespace interi::editor
