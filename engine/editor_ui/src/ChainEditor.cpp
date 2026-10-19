// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ChainEditor.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace interi::editor
{

namespace
{

using chain::ChainAsset;
using chain::ChainLink;
using chain::ChainNode;

constexpr ImU32 kGridColor = IM_COL32(60, 60, 66, 120);
constexpr ImU32 kTriggerColor = IM_COL32(40, 120, 90, 255);
constexpr ImU32 kActionColor = IM_COL32(50, 90, 150, 255);
constexpr ImU32 kUnknownColor = IM_COL32(130, 50, 50, 255);
constexpr ImU32 kLinkColor = IM_COL32(180, 180, 190, 255);
constexpr ImU32 kConditionColor = IM_COL32(240, 170, 60, 255);
constexpr ImU32 kHotColor = IM_COL32(255, 235, 90, 255);
constexpr ImU32 kErrorColor = IM_COL32(230, 70, 60, 255);
constexpr ImU32 kWarningColor = IM_COL32(230, 160, 50, 255);
constexpr ImU32 kSelectColor = IM_COL32(255, 255, 255, 255);

struct View
{
    ImVec2 origin; ///< Screen position of the canvas's top-left.
    std::array<float, 2> pan;
    float zoom;

    [[nodiscard]] ImVec2 ToScreen(float x, float y) const
    {
        return {origin.x + pan[0] + x * zoom, origin.y + pan[1] + y * zoom};
    }
    [[nodiscard]] ImVec2 ToScreen(std::array<float, 2> p) const { return ToScreen(p[0], p[1]); }
    [[nodiscard]] std::array<float, 2> ToCanvas(ImVec2 p) const
    {
        return {(p.x - origin.x - pan[0]) / zoom, (p.y - origin.y - pan[1]) / zoom};
    }
};

ImU32 Lerp(ImU32 a, ImU32 b, float t)
{
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

const chain::EventDefinition* Resolve(const ChainEditorHost& host, std::string_view event)
{
    return host.context.registry == nullptr ? nullptr : host.context.registry->Events().Resolve(event);
}

const ChainNode* FindNode(const ChainAsset& asset, std::string_view id)
{
    const auto found = std::ranges::find(asset.nodes, id, &ChainNode::id);
    return found == asset.nodes.end() ? nullptr : &*found;
}

/// The output a link leaves from (its name; the first when the link names none), or the empty string.
std::size_t OutputIndex(const chain::EventDefinition* definition, const ChainLink& link)
{
    if (definition == nullptr || definition->outputs.empty())
    {
        return 0;
    }
    if (link.fromOutput.empty())
    {
        return 0;
    }
    for (std::size_t i = 0; i < definition->outputs.size(); ++i)
    {
        if (definition->outputs[i].name == link.fromOutput)
        {
            return i;
        }
    }
    return 0;
}

/// Everything per-frame the canvas needs about the asset.
struct Layout
{
    std::vector<ChainRect> rects; ///< By node index.
    std::vector<const chain::EventDefinition*> definitions;
    std::vector<std::pair<std::array<float, 2>, std::array<float, 2>>> links; ///< By link index (canvas units).
};

Layout BuildLayout(const ChainAsset& asset, const ChainEditorHost& host)
{
    Layout layout;
    for (const ChainNode& node : asset.nodes)
    {
        const chain::EventDefinition* definition = Resolve(host, node.event);
        layout.definitions.push_back(definition);
        layout.rects.push_back(ChainNodeRect(node, definition));
    }
    for (const ChainLink& link : asset.links)
    {
        const auto from = std::ranges::find(asset.nodes, link.fromNode, &ChainNode::id);
        const auto to = std::ranges::find(asset.nodes, link.toNode, &ChainNode::id);
        if (from == asset.nodes.end() || to == asset.nodes.end())
        {
            layout.links.emplace_back();
            continue;
        }
        const auto fromIndex = static_cast<std::size_t>(from - asset.nodes.begin());
        const auto toIndex = static_cast<std::size_t>(to - asset.nodes.begin());
        layout.links.emplace_back(
            ChainOutputPin(layout.rects[fromIndex], OutputIndex(layout.definitions[fromIndex], link)),
            ChainInputPin(layout.rects[toIndex]));
    }
    return layout;
}

void DrawCurve(ImDrawList& list, const View& view, std::array<float, 2> from, std::array<float, 2> to, ImU32 color,
               float thickness, bool dashed)
{
    const auto points = ChainLinkCurve(from, to);
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
    {
        if (dashed && i % 2 == 1)
        {
            continue;
        }
        list.AddLine(view.ToScreen(points[i]), view.ToScreen(points[i + 1]), color, thickness);
    }
}

/// Text clipped to `max` pixels wide (a trailing "..." when it doesn't fit).
std::string Fit(std::string text, float max)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= max)
    {
        return text;
    }
    while (text.size() > 1 && ImGui::CalcTextSize((text + "...").c_str()).x > max)
    {
        text.pop_back();
    }
    return text + "...";
}

std::string BindingSummary(const ChainNode& node, const chain::EventParam& input)
{
    const auto found = std::ranges::find(node.params, input.name, &std::pair<std::string, chain::Binding>::first);
    if (found == node.params.end())
    {
        return input.name;
    }
    const chain::Binding& binding = found->second;
    switch (binding.kind)
    {
    case chain::Binding::Kind::Constant:
        return std::format("{} = {}", input.name, binding.text);
    case chain::Binding::Kind::Payload:
        return std::format("{} = payload.{}", input.name, binding.text);
    case chain::Binding::Kind::Value:
        return std::format("{} = value {}", input.name, binding.text);
    case chain::Binding::Kind::Expression:
        return std::format("{} = {}", input.name, binding.text);
    }
    return input.name;
}

void Refresh(ChainEditorState& state, const ChainAsset& asset, const ChainEditorHost& host)
{
    if (!state.cacheValid || !(state.cachedAsset == asset))
    {
        state.diagnostics = ValidateChain(asset, host.context);
        state.cachedAsset = asset;
        state.cacheValid = true;
    }
    if (state.paletteRevision != host.revision && host.context.registry != nullptr)
    {
        state.palette = BuildPalette(*host.context.registry);
        state.paletteRevision = host.revision;
        state.cacheValid = false; // the registry changed: compile again
    }
}

void Report(ChainEditorState& state, const Result<void>& result, bool& edited)
{
    if (result)
    {
        state.message.clear();
        edited = true;
    }
    else
    {
        state.message = result.error().message;
    }
}

// ---- Canvas ------------------------------------------------------------------------------------------------------

void DrawGrid(ImDrawList& list, const View& view, ImVec2 size)
{
    const float step = 32.0f * view.zoom;
    if (step < 6.0f)
    {
        return;
    }
    const float startX = std::fmod(view.pan[0], step);
    for (int column = 0; startX + static_cast<float>(column) * step < size.x; ++column)
    {
        const float x = startX + static_cast<float>(column) * step;
        list.AddLine(ImVec2(view.origin.x + x, view.origin.y), ImVec2(view.origin.x + x, view.origin.y + size.y),
                     kGridColor);
    }
    const float startY = std::fmod(view.pan[1], step);
    for (int row = 0; startY + static_cast<float>(row) * step < size.y; ++row)
    {
        const float y = startY + static_cast<float>(row) * step;
        list.AddLine(ImVec2(view.origin.x, view.origin.y + y), ImVec2(view.origin.x + size.x, view.origin.y + y),
                     kGridColor);
    }
}

/// The group's frame around its member nodes.
bool GroupRect(const chain::ChainGroup& group, const ChainAsset& asset, const Layout& layout, ChainRect& out)
{
    float x0 = std::numeric_limits<float>::max();
    float y0 = x0;
    float x1 = std::numeric_limits<float>::lowest();
    float y1 = x1;
    bool any = false;
    for (const std::string& id : group.nodes)
    {
        const auto found = std::ranges::find(asset.nodes, id, &ChainNode::id);
        if (found == asset.nodes.end())
        {
            continue;
        }
        const ChainRect& r = layout.rects[static_cast<std::size_t>(found - asset.nodes.begin())];
        x0 = std::min(x0, r.x);
        y0 = std::min(y0, r.y);
        x1 = std::max(x1, r.x + r.w);
        y1 = std::max(y1, r.y + r.h);
        any = true;
    }
    out = {x0 - 12.0f, y0 - 28.0f, x1 - x0 + 24.0f, y1 - y0 + 40.0f};
    return any;
}

ImU32 ToU32(const std::array<float, 4>& c)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3]));
}

void DrawMinimap(ImDrawList& list, const View& view, ImVec2 canvasSize, const Layout& layout, ChainEditorState& state,
                 bool hovered)
{
    constexpr ImVec2 kSize(160.0f, 110.0f);
    const ImVec2 low(view.origin.x + canvasSize.x - kSize.x - 8.0f, view.origin.y + canvasSize.y - kSize.y - 8.0f);
    const ImVec2 high(low.x + kSize.x, low.y + kSize.y);
    list.AddRectFilled(low, high, IM_COL32(20, 20, 24, 200));
    list.AddRect(low, high, IM_COL32(120, 120, 130, 255));
    // The world bounds: every node plus the visible area.
    const auto visible0 = view.ToCanvas(view.origin);
    const auto visible1 = view.ToCanvas(ImVec2(view.origin.x + canvasSize.x, view.origin.y + canvasSize.y));
    float x0 = visible0[0];
    float y0 = visible0[1];
    float x1 = visible1[0];
    float y1 = visible1[1];
    for (const ChainRect& r : layout.rects)
    {
        x0 = std::min(x0, r.x);
        y0 = std::min(y0, r.y);
        x1 = std::max(x1, r.x + r.w);
        y1 = std::max(y1, r.y + r.h);
    }
    const float scale = std::min(kSize.x / std::max(x1 - x0, 1.0f), kSize.y / std::max(y1 - y0, 1.0f));
    const auto map = [&](float x, float y) { return ImVec2(low.x + (x - x0) * scale, low.y + (y - y0) * scale); };
    list.PushClipRect(low, high, true);
    for (const ChainRect& r : layout.rects)
    {
        list.AddRectFilled(map(r.x, r.y), map(r.x + r.w, r.y + r.h), IM_COL32(110, 140, 190, 255));
    }
    list.AddRect(map(visible0[0], visible0[1]), map(visible1[0], visible1[1]), IM_COL32(255, 255, 255, 220));
    list.PopClipRect();
    // Click or drag on the minimap centres the view there.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mouse.x >= low.x && mouse.x <= high.x &&
        mouse.y >= low.y && mouse.y <= high.y && !state.draggingNodes && !state.boxSelecting &&
        state.linkFromNode.empty())
    {
        const float cx = x0 + (mouse.x - low.x) / scale;
        const float cy = y0 + (mouse.y - low.y) / scale;
        state.pan = {canvasSize.x * 0.5f - cx * state.zoom, canvasSize.y * 0.5f - cy * state.zoom};
    }
}

struct CanvasOutcome
{
    bool edited = false;
};

CanvasOutcome DrawCanvas(ChainAsset& asset, UndoStack& undo, ChainEditorState& state, const ChainEditorHost& host)
{
    CanvasOutcome outcome;
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    state.canvasOrigin = {origin.x, origin.y};
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    list.AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(28, 28, 32, 255));

    // Zoom around the cursor, pan with the middle button.
    if (hovered && io.MouseWheel != 0.0f)
    {
        const View before{origin, state.pan, state.zoom};
        const auto anchor = before.ToCanvas(io.MousePos);
        state.zoom = std::clamp(state.zoom * std::pow(1.1f, io.MouseWheel), 0.25f, 2.5f);
        state.pan = {io.MousePos.x - origin.x - anchor[0] * state.zoom,
                     io.MousePos.y - origin.y - anchor[1] * state.zoom};
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) && (hovered || ImGui::IsItemActive()))
    {
        state.pan[0] += io.MouseDelta.x;
        state.pan[1] += io.MouseDelta.y;
    }
    const View view{origin, state.pan, state.zoom};
    const Layout layout = BuildLayout(asset, host);
    const auto mouse = view.ToCanvas(io.MousePos);
    DrawGrid(list, view, size);

    // What is under the mouse.
    int nodeUnder = -1;
    for (std::size_t i = layout.rects.size(); i-- > 0;)
    {
        if (layout.rects[i].Contains(mouse[0], mouse[1]))
        {
            nodeUnder = static_cast<int>(i);
            break;
        }
    }
    int pinNode = -1;
    std::size_t pinOutput = 0;
    for (std::size_t i = 0; i < layout.rects.size() && pinNode < 0; ++i)
    {
        const std::size_t outputs = layout.definitions[i] == nullptr ? 0 : layout.definitions[i]->outputs.size();
        for (std::size_t o = 0; o < outputs; ++o)
        {
            const auto pin = ChainOutputPin(layout.rects[i], o);
            if (std::hypot(pin[0] - mouse[0], pin[1] - mouse[1]) <= 8.0f)
            {
                pinNode = static_cast<int>(i);
                pinOutput = o;
                break;
            }
        }
    }
    int linkUnder = -1;
    if (hovered && nodeUnder < 0)
    {
        float best = 8.0f / state.zoom;
        for (std::size_t i = 0; i < layout.links.size(); ++i)
        {
            const float d = ChainDistanceToLink(layout.links[i].first, layout.links[i].second, mouse);
            if (d < best)
            {
                best = d;
                linkUnder = static_cast<int>(i);
            }
        }
    }
    int commentUnder = -1;
    for (std::size_t i = asset.comments.size(); i-- > 0 && nodeUnder < 0;)
    {
        const chain::ChainComment& c = asset.comments[i];
        if (ChainRect{c.pos[0], c.pos[1], c.size[0], c.size[1]}.Contains(mouse[0], mouse[1]))
        {
            commentUnder = static_cast<int>(i);
            break;
        }
    }

    // Clicks.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        state.draggingNodes = state.draggingComment = state.boxSelecting = false;
        if (pinNode >= 0)
        {
            const auto index = static_cast<std::size_t>(pinNode);
            state.linkFromNode = asset.nodes[index].id;
            state.linkFromOutput = layout.definitions[index]->outputs[pinOutput].name;
        }
        else if (nodeUnder >= 0)
        {
            const std::string& id = asset.nodes[static_cast<std::size_t>(nodeUnder)].id;
            if (io.KeyCtrl)
            {
                if (!state.selected.erase(id))
                {
                    state.selected.insert(id);
                }
            }
            else if (!state.selected.contains(id))
            {
                state.selected = {id};
            }
            state.selectedLink = -1;
            state.selectedComment.clear();
            state.draggingNodes = state.selected.contains(id);
        }
        else if (commentUnder >= 0)
        {
            state.selected.clear();
            state.selectedLink = -1;
            state.selectedComment = asset.comments[static_cast<std::size_t>(commentUnder)].id;
            state.draggingComment = true;
        }
        else if (linkUnder >= 0)
        {
            state.selected.clear();
            state.selectedComment.clear();
            state.selectedLink = linkUnder;
        }
        else
        {
            if (!io.KeyCtrl)
            {
                state.selected.clear();
            }
            state.selectedLink = -1;
            state.selectedComment.clear();
            state.boxSelecting = true;
            state.boxStart = mouse;
        }
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && (state.draggingNodes || state.draggingComment))
    {
        const std::array<float, 2> delta = {io.MouseDelta.x / state.zoom, io.MouseDelta.y / state.zoom};
        std::vector<std::string> nodes(state.selected.begin(), state.selected.end());
        std::vector<std::string> comments;
        if (!state.selectedComment.empty())
        {
            comments.push_back(state.selectedComment);
        }
        if (state.draggingComment)
        {
            nodes.clear();
        }
        if (MoveChainNodes(undo, asset, nodes, delta, comments))
        {
            outcome.edited = true;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (state.draggingNodes || state.draggingComment)
        {
            undo.Seal();
        }
        if (!state.linkFromNode.empty())
        {
            if (nodeUnder >= 0 && asset.nodes[static_cast<std::size_t>(nodeUnder)].id != state.linkFromNode)
            {
                Report(state,
                       AddChainLink(undo, asset, host.context, state.linkFromNode, state.linkFromOutput,
                                    asset.nodes[static_cast<std::size_t>(nodeUnder)].id),
                       outcome.edited);
            }
            state.linkFromNode.clear();
        }
        if (state.boxSelecting)
        {
            const float x0 = std::min(state.boxStart[0], mouse[0]);
            const float x1 = std::max(state.boxStart[0], mouse[0]);
            const float y0 = std::min(state.boxStart[1], mouse[1]);
            const float y1 = std::max(state.boxStart[1], mouse[1]);
            for (std::size_t i = 0; i < layout.rects.size(); ++i)
            {
                const ChainRect& r = layout.rects[i];
                if (r.x < x1 && r.x + r.w > x0 && r.y < y1 && r.y + r.h > y0)
                {
                    state.selected.insert(asset.nodes[i].id);
                }
            }
        }
        state.draggingNodes = state.draggingComment = state.boxSelecting = false;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        state.contextPos = mouse;
        state.contextNode = nodeUnder >= 0 ? asset.nodes[static_cast<std::size_t>(nodeUnder)].id : std::string();
        ImGui::OpenPopup(nodeUnder >= 0 ? "chain_node_menu" : "chain_canvas_menu");
    }

    // Keyboard.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
        {
            if (!state.selected.empty())
            {
                const std::vector<std::string> ids(state.selected.begin(), state.selected.end());
                Report(state, RemoveChainNodes(undo, asset, ids), outcome.edited);
                state.selected.clear();
            }
            else if (state.selectedLink >= 0)
            {
                Report(state, RemoveChainLink(undo, asset, static_cast<std::size_t>(state.selectedLink)),
                       outcome.edited);
                state.selectedLink = -1;
            }
            else if (!state.selectedComment.empty())
            {
                Report(state, RemoveChainComment(undo, asset, state.selectedComment), outcome.edited);
                state.selectedComment.clear();
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            state.clipboard = CopyChainNodes(asset, {state.selected.begin(), state.selected.end()});
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !state.clipboard.Empty())
        {
            if (auto pasted = PasteChainNodes(undo, asset, state.clipboard, {32.0f, 32.0f}))
            {
                state.selected = {pasted->begin(), pasted->end()};
                outcome.edited = true;
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G) && !state.selected.empty())
        {
            Report(state,
                   GroupChainNodes(undo, asset, {state.selected.begin(), state.selected.end()}, "Group")
                       .transform([](const std::string&) {}),
                   outcome.edited);
        }
    }

    // An edit above changed the node or link count: the layout is stale, so draw again next frame.
    if (layout.rects.size() != asset.nodes.size() || layout.links.size() != asset.links.size())
    {
        list.PopClipRect();
        outcome.edited = true;
        return outcome;
    }

    // Groups and comments sit behind the nodes.
    for (const chain::ChainGroup& group : asset.groups)
    {
        ChainRect r;
        if (!GroupRect(group, asset, layout, r))
        {
            continue;
        }
        const ImVec2 low = view.ToScreen(r.x, r.y);
        const ImVec2 high = view.ToScreen(r.x + r.w, r.y + r.h);
        list.AddRectFilled(low, high, ToU32(group.color), 6.0f);
        list.AddRect(low, high, IM_COL32(255, 255, 255, 60), 6.0f);
        list.AddText(ImVec2(low.x + 8.0f, low.y + 4.0f), IM_COL32(230, 230, 235, 255), group.title.c_str());
    }
    for (const chain::ChainComment& comment : asset.comments)
    {
        const ImVec2 low = view.ToScreen(comment.pos);
        const ImVec2 high = view.ToScreen(comment.pos[0] + comment.size[0], comment.pos[1] + comment.size[1]);
        list.AddRectFilled(low, high, IM_COL32(110, 100, 40, 160), 3.0f);
        list.AddRect(low, high, comment.id == state.selectedComment ? kSelectColor : IM_COL32(200, 190, 90, 255), 3.0f);
        list.PushClipRect(low, high, true);
        list.AddText(ImVec2(low.x + 6.0f, low.y + 4.0f), IM_COL32(255, 250, 200, 255), comment.text.c_str());
        list.PopClipRect();
    }

    // Links.
    const ChainDebugger* debugger = host.debugger;
    for (std::size_t i = 0; i < asset.links.size(); ++i)
    {
        const ChainLink& link = asset.links[i];
        const auto& [from, to] = layout.links[i];
        const bool conditional = !link.condition.empty();
        ImU32 color = conditional ? kConditionColor : kLinkColor;
        float thickness = 2.0f * state.zoom;
        if (debugger != nullptr)
        {
            const float heat = debugger->LinkHeat(i, host.playStep);
            if (heat > 0.0f)
            {
                color = Lerp(color, debugger->LinkPassed(i) ? kHotColor : kErrorColor, heat);
                thickness += 2.0f * heat;
            }
        }
        if (!DiagnosticsForLink(state.diagnostics, i).empty())
        {
            color = kErrorColor;
        }
        if (std::cmp_equal(i, state.selectedLink))
        {
            thickness += 2.0f;
            color = Lerp(color, kSelectColor, 0.6f);
        }
        DrawCurve(list, view, from, to, color, thickness, conditional);
        if (conditional)
        {
            const ImVec2 mid = view.ToScreen((from[0] + to[0]) * 0.5f, (from[1] + to[1]) * 0.5f);
            const std::string badge = "if " + Fit(link.condition, 140.0f);
            const ImVec2 text = ImGui::CalcTextSize(badge.c_str());
            list.AddRectFilled(ImVec2(mid.x - 4.0f, mid.y - text.y * 0.5f - 2.0f),
                               ImVec2(mid.x + text.x + 4.0f, mid.y + text.y * 0.5f + 2.0f), IM_COL32(60, 45, 15, 230),
                               4.0f);
            list.AddText(ImVec2(mid.x, mid.y - text.y * 0.5f), kConditionColor, badge.c_str());
        }
        if (debugger != nullptr && debugger->HasLinkBreakpoint(i))
        {
            const ImVec2 mid = view.ToScreen((from[0] + to[0]) * 0.5f, (from[1] + to[1]) * 0.5f);
            list.AddCircleFilled(ImVec2(mid.x - 10.0f, mid.y - 10.0f), 5.0f, kErrorColor);
        }
    }
    if (!state.linkFromNode.empty())
    {
        const auto from = std::ranges::find(asset.nodes, state.linkFromNode, &ChainNode::id);
        if (from != asset.nodes.end())
        {
            const auto index = static_cast<std::size_t>(from - asset.nodes.begin());
            std::size_t output = 0;
            if (layout.definitions[index] != nullptr)
            {
                for (std::size_t o = 0; o < layout.definitions[index]->outputs.size(); ++o)
                {
                    if (layout.definitions[index]->outputs[o].name == state.linkFromOutput)
                    {
                        output = o;
                    }
                }
            }
            DrawCurve(list, view, ChainOutputPin(layout.rects[index], output), mouse, kSelectColor, 2.0f, false);
        }
    }

    // Nodes.
    for (std::size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const ChainNode& node = asset.nodes[i];
        const chain::EventDefinition* definition = layout.definitions[i];
        const ChainRect& r = layout.rects[i];
        const ImVec2 low = view.ToScreen(r.x, r.y);
        const ImVec2 high = view.ToScreen(r.x + r.w, r.y + r.h);
        const ImVec2 headerHigh = view.ToScreen(r.x + r.w, r.y + kChainNodeHeader);
        const bool selected = state.selected.contains(node.id);
        const auto problems = DiagnosticsForNode(state.diagnostics, node.id);
        bool hasError = false;
        for (const chain::Diagnostic* d : problems)
        {
            hasError |= d->severity == chain::Diagnostic::Severity::Error;
        }
        list.AddRectFilled(low, high, IM_COL32(40, 40, 46, 245), 4.0f);
        ImU32 header = definition == nullptr
                           ? kUnknownColor
                           : (definition->kind == chain::EventKind::Trigger ? kTriggerColor : kActionColor);
        if (debugger != nullptr)
        {
            header = Lerp(header, kHotColor, debugger->NodeHeat(node.id, host.playStep) * 0.9f);
        }
        list.AddRectFilled(low, headerHigh, header, 4.0f, ImDrawFlags_RoundCornersTop);
        list.PushClipRect(low, high, true);
        list.AddText(ImVec2(low.x + 14.0f * state.zoom, low.y + 4.0f * state.zoom), IM_COL32(255, 255, 255, 255),
                     Fit(node.id, (kChainNodeWidth - 24.0f) * state.zoom).c_str());
        if (definition != nullptr)
        {
            for (std::size_t in = 0; in < definition->inputs.size(); ++in)
            {
                list.AddText(
                    view.ToScreen(r.x + 8.0f, r.y + kChainNodeHeader + static_cast<float>(in) * kChainNodeRow + 3.0f),
                    IM_COL32(190, 190, 200, 255),
                    Fit(BindingSummary(node, definition->inputs[in]), (kChainNodeWidth * 0.6f) * state.zoom).c_str());
            }
            for (std::size_t o = 0; o < definition->outputs.size(); ++o)
            {
                const std::string& name = definition->outputs[o].name;
                const ImVec2 text = ImGui::CalcTextSize(name.c_str());
                const auto pin = ChainOutputPin(r, o);
                const ImVec2 p = view.ToScreen(pin);
                list.AddText(ImVec2(p.x - text.x - 10.0f, p.y - text.y * 0.5f), IM_COL32(210, 210, 220, 255),
                             name.c_str());
            }
        }
        else
        {
            list.AddText(view.ToScreen(r.x + 8.0f, r.y + kChainNodeHeader + 3.0f), IM_COL32(230, 150, 150, 255),
                         Fit("unknown: " + node.event, (kChainNodeWidth - 16.0f) * state.zoom).c_str());
        }
        list.PopClipRect();
        const ImU32 border =
            hasError ? kErrorColor
                     : (!problems.empty() ? kWarningColor : (selected ? kSelectColor : IM_COL32(20, 20, 24, 255)));
        list.AddRect(low, high, border, 4.0f, 0, selected || hasError ? 2.0f : 1.0f);
        // Pins.
        if (definition == nullptr || definition->kind == chain::EventKind::Action)
        {
            list.AddCircleFilled(view.ToScreen(ChainInputPin(r)), 5.0f * state.zoom, IM_COL32(200, 200, 210, 255));
        }
        if (definition != nullptr)
        {
            for (std::size_t o = 0; o < definition->outputs.size(); ++o)
            {
                const bool grabbing = std::cmp_equal(i, pinNode) && pinOutput == o && hovered;
                list.AddCircleFilled(view.ToScreen(ChainOutputPin(r, o)), (grabbing ? 7.0f : 5.0f) * state.zoom,
                                     grabbing ? kSelectColor : IM_COL32(200, 200, 210, 255));
            }
        }
        if (debugger != nullptr && debugger->HasNodeBreakpoint(node.id))
        {
            list.AddCircleFilled(ImVec2(low.x + 6.0f, low.y + 6.0f), 4.5f * state.zoom, kErrorColor);
        }
    }
    if (hovered && nodeUnder >= 0 && pinNode < 0)
    {
        const auto index = static_cast<std::size_t>(nodeUnder);
        const auto problems = DiagnosticsForNode(state.diagnostics, asset.nodes[index].id);
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(asset.nodes[index].event.c_str());
        if (const auto entry = std::ranges::find(state.palette, asset.nodes[index].event, &PaletteEntry::id);
            entry != state.palette.end() && !entry->indicator.empty())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", entry->indicator.c_str());
        }
        for (const chain::Diagnostic* d : problems)
        {
            ImGui::TextColored(d->severity == chain::Diagnostic::Severity::Error ? ImVec4(1.0f, 0.4f, 0.3f, 1.0f)
                                                                                 : ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                               "%s", d->Text().c_str());
        }
        ImGui::EndTooltip();
    }
    if (state.boxSelecting)
    {
        list.AddRectFilled(view.ToScreen(state.boxStart), io.MousePos, IM_COL32(120, 160, 255, 40));
        list.AddRect(view.ToScreen(state.boxStart), io.MousePos, IM_COL32(120, 160, 255, 200));
    }
    if (state.showMinimap)
    {
        DrawMinimap(list, view, size, layout, state, hovered);
    }
    list.PopClipRect();

    // Drag an event from the palette onto the canvas.
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHAIN_EVENT"))
        {
            const std::string event(static_cast<const char*>(payload->Data),
                                    static_cast<std::size_t>(payload->DataSize) - 1);
            if (auto id = AddChainNode(undo, asset, host.context, event, mouse))
            {
                state.selected = {*id};
                outcome.edited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Menus.
    if (ImGui::BeginPopup("chain_canvas_menu"))
    {
        ImGui::TextUnformatted("Add event");
        ImGui::Separator();
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputTextWithHint("##search", "search", &state.paletteQuery);
        int shown = 0;
        for (const PaletteEntry* entry : FilterPalette(state.palette, state.paletteQuery))
        {
            if (++shown > 12)
            {
                break;
            }
            if (ImGui::MenuItem(entry->id.c_str()))
            {
                if (auto id = AddChainNode(undo, asset, host.context, entry->id, state.contextPos))
                {
                    state.selected = {*id};
                    outcome.edited = true;
                }
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Add comment"))
        {
            if (AddChainComment(undo, asset, state.contextPos, "Comment"))
            {
                outcome.edited = true;
            }
        }
        if (ImGui::MenuItem("Paste", nullptr, false, !state.clipboard.Empty()))
        {
            if (PasteChainNodes(undo, asset, state.clipboard,
                                {state.contextPos[0] - state.clipboard.nodes.front().pos[0],
                                 state.contextPos[1] - state.clipboard.nodes.front().pos[1]}))
            {
                outcome.edited = true;
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("chain_node_menu"))
    {
        const std::string id = state.contextNode;
        if (ImGui::MenuItem("Delete"))
        {
            Report(state, RemoveChainNodes(undo, asset, {id}), outcome.edited);
            state.selected.erase(id);
        }
        if (ImGui::MenuItem("Copy"))
        {
            state.clipboard = CopyChainNodes(asset, {id});
        }
        if (ImGui::MenuItem("Group selected", nullptr, false, !state.selected.empty()))
        {
            const auto group = GroupChainNodes(undo, asset, {state.selected.begin(), state.selected.end()}, "Group");
            outcome.edited |= group.has_value();
        }
        if (debugger != nullptr && ImGui::MenuItem("Toggle breakpoint"))
        {
            host.debugger->ToggleNodeBreakpoint(id);
        }
        ImGui::EndPopup();
    }
    return outcome;
}

// ---- Palette -----------------------------------------------------------------------------------------------------

void DrawPalette(ChainAsset& asset, UndoStack& undo, ChainEditorState& state, const ChainEditorHost& host, bool& edited)
{
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##palette", "search events", &state.paletteQuery);
    const auto hits = FilterPalette(state.palette, state.paletteQuery);
    const bool searching = !state.paletteQuery.empty();
    std::string module;
    std::string category;
    bool open = false;
    for (const PaletteEntry* entry : hits)
    {
        if (entry->module != module)
        {
            if (open)
            {
                ImGui::TreePop();
            }
            module = entry->module;
            category.clear();
            if (searching)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            open = ImGui::TreeNode(module.c_str());
        }
        if (!open)
        {
            continue;
        }
        if (entry->category != category)
        {
            category = entry->category;
            if (!category.empty())
            {
                ImGui::TextDisabled("%s", category.c_str());
            }
        }
        const bool trigger = entry->kind == chain::EventKind::Trigger;
        ImGui::PushStyleColor(ImGuiCol_Text,
                              trigger ? ImVec4(0.5f, 0.9f, 0.7f, 1.0f) : ImVec4(0.75f, 0.85f, 1.0f, 1.0f));
        const bool clicked = ImGui::Selectable(entry->id.c_str());
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("CHAIN_EVENT", entry->id.c_str(), entry->id.size() + 1);
            ImGui::TextUnformatted(entry->id.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && (!entry->tooltip.empty() || !entry->indicator.empty()))
        {
            ImGui::BeginTooltip();
            if (!entry->tooltip.empty())
            {
                ImGui::TextUnformatted(entry->tooltip.c_str());
            }
            if (!entry->indicator.empty())
            {
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", entry->indicator.c_str());
            }
            ImGui::EndTooltip();
        }
        if (!entry->indicator.empty())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "  %s", entry->indicator.c_str());
        }
        if (clicked)
        {
            if (auto id = AddChainNode(undo, asset, host.context, entry->id,
                                       {-state.pan[0] / state.zoom + 220.0f, -state.pan[1] / state.zoom + 120.0f}))
            {
                state.selected = {*id};
                edited = true;
            }
            else
            {
                state.message = id.error().message;
            }
        }
    }
    if (open)
    {
        ImGui::TreePop();
    }
}

// ---- Inspector ---------------------------------------------------------------------------------------------------

/// A starting expression of `type`.
std::string ExpressionDefault(values::ValueType type)
{
    switch (type)
    {
    case values::ValueType::Bool:
        return "true";
    case values::ValueType::Vec3:
        return "vec3(0, 0, 0)";
    case values::ValueType::String:
    case values::ValueType::Enum:
    case values::ValueType::AssetRef:
        return "\"\"";
    default:
        return "0";
    }
}

constexpr const char* kKindNames[] = {"default", "constant", "payload", "value", "expression"};

void DrawInputBinding(ChainAsset& asset, UndoStack& undo, ChainEditorState& state, const ChainEditorHost& host,
                      const ChainNode& node, const chain::EventParam& input, bool& edited)
{
    ImGui::PushID(input.name.c_str());
    const auto found = std::ranges::find(node.params, input.name, &std::pair<std::string, chain::Binding>::first);
    int kind = found == node.params.end() ? 0 : static_cast<int>(found->second.kind) + 1;
    const std::string key = node.id + "." + input.name;
    const values::ValueType type = input.schema.type;
    ImGui::TextUnformatted(input.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "(%s)", std::string(values::ToString(input.typeFrom.empty() ? type : values::ValueType::String)).c_str());
    ImGui::SetNextItemWidth(110.0f);
    int chosen = kind;
    if (ImGui::Combo("##kind", &chosen, kKindNames, 5) && chosen != kind)
    {
        std::optional<chain::Binding> binding;
        bool ok = true;
        switch (chosen)
        {
        case 1:
            binding = chain::Binding{.kind = chain::Binding::Kind::Constant,
                                     .text = input.typeFrom.empty() ? values::ValueToJsonText(input.schema.defaultValue)
                                                                    : "\"\""};
            break;
        case 2:
        {
            const auto choices = FilterChoices(PayloadChoices(asset, host.context, node.id), type);
            ok = !choices.empty();
            if (ok)
            {
                binding = chain::Binding{.kind = chain::Binding::Kind::Payload, .text = choices.front().name};
            }
            else
            {
                state.bindingErrors[key] = "no payload field of this type comes in through the links";
            }
            break;
        }
        case 3:
        {
            const auto choices = FilterChoices(ValueChoices(host.context.values), type);
            ok = !choices.empty();
            if (ok)
            {
                binding = chain::Binding{.kind = chain::Binding::Kind::Value, .text = choices.front().name};
            }
            else
            {
                state.bindingErrors[key] = "the project defines no Value of this type";
            }
            break;
        }
        case 4:
            binding = chain::Binding{.kind = chain::Binding::Kind::Expression, .text = ExpressionDefault(type)};
            break;
        default:
            break;
        }
        if (ok)
        {
            const auto result = BindChainInput(undo, asset, host.context, node.id, input.name, binding);
            if (result)
            {
                state.bindingErrors.erase(key);
                edited = true;
            }
            else
            {
                state.bindingErrors[key] = result.error().message;
            }
            undo.Seal();
        }
    }
    if (found != node.params.end())
    {
        chain::Binding binding = found->second;
        bool changed = false;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        switch (binding.kind)
        {
        case chain::Binding::Kind::Constant:
        case chain::Binding::Kind::Expression:
            changed = ImGui::InputText("##text", &binding.text);
            break;
        case chain::Binding::Kind::Payload:
        case chain::Binding::Kind::Value:
        {
            const auto choices = FilterChoices(binding.kind == chain::Binding::Kind::Payload
                                                   ? PayloadChoices(asset, host.context, node.id)
                                                   : ValueChoices(host.context.values),
                                               type);
            if (ImGui::BeginCombo("##pick", binding.text.c_str()))
            {
                for (const BindingChoice& choice : choices)
                {
                    if (ImGui::Selectable(std::format("{} ({})", choice.name, values::ToString(choice.type)).c_str(),
                                          choice.name == binding.text))
                    {
                        binding.text = choice.name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        }
        if (changed)
        {
            const auto result = BindChainInput(undo, asset, host.context, node.id, input.name, binding);
            if (result)
            {
                state.bindingErrors.erase(key);
                edited = true;
            }
            else
            {
                state.bindingErrors[key] = result.error().message;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            undo.Seal();
        }
    }
    if (const auto error = state.bindingErrors.find(key); error != state.bindingErrors.end())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", error->second.c_str());
    }
    ImGui::PopID();
}

void DrawInspector(ChainAsset& asset, UndoStack& undo, ChainEditorState& state, const ChainEditorHost& host,
                   bool& edited)
{
    int priority = asset.priority;
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("priority", &priority))
    {
        Report(state, SetChainPriority(undo, asset, priority), edited);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        undo.Seal();
    }
    ImGui::Separator();
    if (state.selected.size() == 1)
    {
        const ChainNode* node = FindNode(asset, *state.selected.begin());
        if (node == nullptr)
        {
            return;
        }
        const std::string id = node->id;
        std::string name = id;
        if (ImGui::InputText("id", &name, ImGuiInputTextFlags_EnterReturnsTrue) ||
            (ImGui::IsItemDeactivatedAfterEdit() && name != id))
        {
            const auto result = RenameChainNode(undo, asset, id, name);
            Report(state, result, edited);
            if (result)
            {
                state.selected = {name};
                return;
            }
        }
        ImGui::TextDisabled("%s", node->event.c_str());
        if (const auto entry = std::ranges::find(state.palette, node->event, &PaletteEntry::id);
            entry != state.palette.end() && !entry->indicator.empty())
        {
            ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", entry->indicator.c_str());
        }
        if (host.debugger != nullptr)
        {
            bool breakpoint = host.debugger->HasNodeBreakpoint(id);
            if (ImGui::Checkbox("breakpoint", &breakpoint))
            {
                host.debugger->ToggleNodeBreakpoint(id);
            }
        }
        if (const chain::EventDefinition* definition = Resolve(host, node->event))
        {
            if (!definition->tooltip.empty())
            {
                ImGui::TextWrapped("%s", definition->tooltip.c_str());
            }
            ImGui::SeparatorText(definition->kind == chain::EventKind::Trigger ? "trigger inputs" : "inputs");
            for (const chain::EventParam& input : definition->inputs)
            {
                DrawInputBinding(asset, undo, state, host, *node, input, edited);
                node = FindNode(asset, id); // an edit replaced the asset's nodes
                if (node == nullptr)
                {
                    return;
                }
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "No loaded module provides this event.");
        }
    }
    else if (state.selected.size() > 1)
    {
        ImGui::Text("%zu nodes selected", state.selected.size());
        if (ImGui::Button("Group"))
        {
            const auto group = GroupChainNodes(undo, asset, {state.selected.begin(), state.selected.end()}, "Group");
            edited |= group.has_value();
        }
    }
    else if (state.selectedLink >= 0 && static_cast<std::size_t>(state.selectedLink) < asset.links.size())
    {
        const auto index = static_cast<std::size_t>(state.selectedLink);
        const ChainLink& link = asset.links[index];
        ImGui::Text("%s%s%s  ->  %s", link.fromNode.c_str(), link.fromOutput.empty() ? "" : ".",
                    link.fromOutput.c_str(), link.toNode.c_str());
        std::string condition = link.condition;
        ImGui::TextUnformatted("condition (a bool expression; empty = always)");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##condition", &condition))
        {
            Report(state, SetChainLinkCondition(undo, asset, host.context, index, condition), edited);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            undo.Seal();
        }
        const auto fields = PayloadChoices(asset, host.context, link.toNode);
        (void)fields;
        if (const auto source = FindNode(asset, link.fromNode))
        {
            if (const chain::EventDefinition* definition = Resolve(host, source->event); definition != nullptr)
            {
                const std::size_t output = OutputIndex(definition, link);
                if (output < definition->outputs.size() && !definition->outputs[output].payload.empty())
                {
                    ImGui::TextDisabled("payload:");
                    for (const chain::PayloadField& field : definition->outputs[output].payload)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(std::format("{}: {}", field.name, values::ToString(field.type)).c_str()))
                        {
                            condition += (condition.empty() ? "" : " ") + std::string("payload.") + field.name;
                            Report(state, SetChainLinkCondition(undo, asset, host.context, index, condition), edited);
                            undo.Seal();
                        }
                    }
                }
            }
        }
        if (host.debugger != nullptr)
        {
            bool breakpoint = host.debugger->HasLinkBreakpoint(index);
            if (ImGui::Checkbox("breakpoint", &breakpoint))
            {
                host.debugger->ToggleLinkBreakpoint(index);
            }
        }
        if (ImGui::Button("Remove link"))
        {
            Report(state, RemoveChainLink(undo, asset, index), edited);
            state.selectedLink = -1;
        }
    }
    else if (!state.selectedComment.empty())
    {
        const auto found = std::ranges::find(asset.comments, state.selectedComment, &chain::ChainComment::id);
        if (found != asset.comments.end())
        {
            chain::ChainComment comment = *found;
            bool changed = ImGui::InputTextMultiline("##text", &comment.text, ImVec2(-1.0f, 80.0f));
            changed |= ImGui::DragFloat2("size", comment.size.data(), 1.0f, 40.0f, 2000.0f);
            if (changed)
            {
                Report(state, EditChainComment(undo, asset, state.selectedComment, comment), edited);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                undo.Seal();
            }
        }
    }
    else
    {
        ImGui::TextDisabled("Select a node or a link.");
    }
    if (!asset.groups.empty())
    {
        ImGui::SeparatorText("groups");
        for (const chain::ChainGroup& group : asset.groups)
        {
            ImGui::PushID(group.id.c_str());
            chain::ChainGroup copy = group;
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::InputText("##title", &copy.title))
            {
                Report(state, EditChainGroup(undo, asset, group.id, copy), edited);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                undo.Seal();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ungroup"))
            {
                Report(state, UngroupChain(undo, asset, group.id), edited);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }
}

// ---- Bottom tabs ---------------------------------------------------------------------------------------------------

const char* KindName(chain::TraceRecord::Kind kind)
{
    using Kind = chain::TraceRecord::Kind;
    switch (kind)
    {
    case Kind::Fired:
        return "fired";
    case Kind::Condition:
        return "condition";
    case Kind::Ran:
        return "ran";
    case Kind::AuthoritySkip:
        return "skipped";
    case Kind::Dropped:
        return "dropped";
    case Kind::WriteRejected:
        return "rejected";
    }
    return "";
}

void DrawBottom(ChainAsset& asset, ChainEditorState& state, const ChainEditorHost& host)
{
    if (!ImGui::BeginTabBar("chain_bottom"))
    {
        return;
    }
    const std::string problems = std::format("Problems ({})###problems", state.diagnostics.size());
    if (ImGui::BeginTabItem(problems.c_str()))
    {
        for (const chain::Diagnostic& d : state.diagnostics)
        {
            const bool error = d.severity == chain::Diagnostic::Severity::Error;
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  error ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
            const std::string text = d.Text();
            if (ImGui::Selectable(text.c_str()) && !d.node.empty() && FindNode(asset, d.node) != nullptr)
            {
                state.selected = {d.node};
                state.selectedLink = d.link ? static_cast<int>(*d.link) : -1;
                if (state.selectedLink >= 0)
                {
                    state.selected.clear();
                }
            }
            ImGui::PopStyleColor();
        }
        if (state.diagnostics.empty())
        {
            ImGui::TextDisabled("No problems.");
        }
        ImGui::EndTabItem();
    }
    if (host.debugger != nullptr)
    {
        ChainDebugger& debugger = *host.debugger;
        if (ImGui::BeginTabItem("Timeline"))
        {
            if (!debugger.LastHit().empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Breakpoint: %s", debugger.LastHit().c_str());
            }
            if (ImGui::BeginTable("timeline", 5,
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("step", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("node", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("detail", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("payload");
                ImGui::TableHeadersRow();
                for (const TimelineEntry& entry : debugger.Timeline())
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(entry.step));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.kind == chain::TraceRecord::Kind::Condition
                                               ? (entry.passed ? "if: true" : "if: false")
                                               : KindName(entry.kind));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.node.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.detail.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.payload.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Watch"))
        {
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::InputTextWithHint("##watch", "Value id, Enter to watch", &state.watchInput,
                                         ImGuiInputTextFlags_EnterReturnsTrue))
            {
                debugger.AddWatch(state.watchInput);
                state.watchInput.clear();
            }
            if (host.values != nullptr)
            {
                std::string remove;
                for (const WatchValue& watch : debugger.ReadWatches(*host.values))
                {
                    ImGui::Text("%s = %s", watch.id.c_str(), watch.text.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(("x##" + watch.id).c_str()))
                    {
                        remove = watch.id;
                    }
                }
                if (!remove.empty())
                {
                    debugger.RemoveWatch(remove);
                }
            }
            ImGui::EndTabItem();
        }
    }
    ImGui::EndTabBar();
}

} // namespace

// ---- Public geometry
// -------------------------------------------------------------------------------------------------

ChainRect ChainNodeRect(const ChainNode& node, const chain::EventDefinition* definition)
{
    std::size_t rows = 1;
    if (definition != nullptr)
    {
        rows = std::max<std::size_t>({1, definition->inputs.size(), definition->outputs.size()});
    }
    return {node.pos[0], node.pos[1], kChainNodeWidth,
            kChainNodeHeader + static_cast<float>(rows) * kChainNodeRow + 6.0f};
}

std::array<float, 2> ChainOutputPin(const ChainRect& rect, std::size_t index)
{
    return {rect.x + rect.w, rect.y + kChainNodeHeader + (static_cast<float>(index) + 0.5f) * kChainNodeRow};
}

std::array<float, 2> ChainInputPin(const ChainRect& rect)
{
    return {rect.x, rect.y + kChainNodeHeader * 0.5f};
}

std::vector<std::array<float, 2>> ChainLinkCurve(std::array<float, 2> from, std::array<float, 2> to, int segments)
{
    const float reach = std::max(40.0f, std::abs(to[0] - from[0]) * 0.5f);
    const std::array<float, 2> c1 = {from[0] + reach, from[1]};
    const std::array<float, 2> c2 = {to[0] - reach, to[1]};
    std::vector<std::array<float, 2>> points;
    for (int i = 0; i <= segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float u = 1.0f - t;
        const float a = u * u * u;
        const float b = 3.0f * u * u * t;
        const float c = 3.0f * u * t * t;
        const float d = t * t * t;
        points.push_back(
            {a * from[0] + b * c1[0] + c * c2[0] + d * to[0], a * from[1] + b * c1[1] + c * c2[1] + d * to[1]});
    }
    return points;
}

float ChainDistanceToLink(std::array<float, 2> from, std::array<float, 2> to, std::array<float, 2> point)
{
    const auto points = ChainLinkCurve(from, to);
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i + 1 < points.size(); ++i)
    {
        const float ax = points[i][0];
        const float ay = points[i][1];
        const float dx = points[i + 1][0] - ax;
        const float dy = points[i + 1][1] - ay;
        const float lengthSquared = dx * dx + dy * dy;
        const float t = lengthSquared <= 0.0f
                            ? 0.0f
                            : std::clamp(((point[0] - ax) * dx + (point[1] - ay) * dy) / lengthSquared, 0.0f, 1.0f);
        best = std::min(best, std::hypot(point[0] - (ax + t * dx), point[1] - (ay + t * dy)));
    }
    return best;
}

ChainEditorResult DrawChainEditor(ChainAsset& asset, UndoStack& undo, ChainEditorState& state,
                                  const ChainEditorHost& host)
{
    ChainEditorResult result;
    ImGui::SetNextWindowSize(ImVec2(1180.0f, 640.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kChainEditorWindow))
    {
        ImGui::End();
        return result;
    }
    Refresh(state, asset, host);

    // Toolbar.
    if (ImGui::Button("Undo") && undo.CanUndo())
    {
        undo.Undo();
        result.edited = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo") && undo.CanRedo())
    {
        undo.Redo();
        result.edited = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Group") && !state.selected.empty())
    {
        result.edited |=
            GroupChainNodes(undo, asset, {state.selected.begin(), state.selected.end()}, "Group").has_value();
    }
    ImGui::SameLine();
    if (ImGui::Button("Comment"))
    {
        result.edited |=
            AddChainComment(undo, asset, {-state.pan[0] / state.zoom + 40.0f, -state.pan[1] / state.zoom + 40.0f},
                            "Comment")
                .has_value();
    }
    ImGui::SameLine();
    ImGui::Checkbox("minimap", &state.showMinimap);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::SliderFloat("zoom", &state.zoom, 0.25f, 2.5f, "%.2f");
    if (host.debugger != nullptr)
    {
        ImGui::SameLine();
        if (host.playing)
        {
            if (ImGui::Button(host.paused ? "Continue" : "Pause"))
            {
                (host.paused ? result.resumeRequested : result.pauseRequested) = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Step"))
            {
                result.stepRequested = true;
            }
            ImGui::SameLine();
            ImGui::Text("step %llu", static_cast<unsigned long long>(host.playStep));
        }
        else
        {
            ImGui::TextDisabled("press Play to debug");
        }
    }
    if (!state.message.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", state.message.c_str());
    }

    const float bottom = 150.0f;
    if (ImGui::BeginChild("chain_top", ImVec2(0.0f, -bottom)))
    {
        if (ImGui::BeginChild("chain_palette", ImVec2(230.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawPalette(asset, undo, state, host, result.edited);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        const float inspector = 280.0f;
        if (ImGui::BeginChild("chain_canvas", ImVec2(-inspector - ImGui::GetStyle().ItemSpacing.x, 0.0f),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            result.edited |= DrawCanvas(asset, undo, state, host).edited;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("chain_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawInspector(asset, undo, state, host, result.edited);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("chain_bottom_panel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        DrawBottom(asset, state, host);
    }
    ImGui::EndChild();
    ImGui::End();
    return result;
}

} // namespace interi::editor
