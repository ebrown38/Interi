// SPDX-License-Identifier: MIT
#include <interi/editor/HudEditing.h>

#include <algorithm>
#include <format>
#include <memory>

namespace interi::editor
{

namespace
{

/// Sets `*document` to `after` and back. `mergeKey` (non-empty) lets consecutive edits of one widget merge.
class HudSnapshotCommand final : public ICommand
{
public:
    HudSnapshotCommand(hud::HudDocument* document, hud::HudDocument after, std::string label, std::string mergeKey)
        : m_document(document)
        , m_before(*document)
        , m_after(std::move(after))
        , m_label(std::move(label))
        , m_mergeKey(std::move(mergeKey))
    {
    }

    void Do() override { *m_document = m_after; }
    void Undo() override { *m_document = m_before; }
    [[nodiscard]] std::string_view Label() const override { return m_label; }

    bool MergeWith(const ICommand& next) override
    {
        const auto* other = dynamic_cast<const HudSnapshotCommand*>(&next);
        if (other == nullptr || m_mergeKey.empty() || other->m_mergeKey != m_mergeKey ||
            other->m_document != m_document)
        {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    hud::HudDocument* m_document;
    hud::HudDocument m_before;
    hud::HudDocument m_after;
    std::string m_label;
    std::string m_mergeKey;
};

void Apply(UndoStack& undo, hud::HudDocument& document, hud::HudDocument after, std::string label,
           std::string mergeKey = {})
{
    undo.Execute(
        std::make_unique<HudSnapshotCommand>(&document, std::move(after), std::move(label), std::move(mergeKey)));
}

std::size_t IndexOf(const hud::HudDocument& document, std::string_view id)
{
    const auto found = std::ranges::find(document.widgets, id, &hud::HudWidget::id);
    return static_cast<std::size_t>(found - document.widgets.begin());
}

std::string UniqueId(const hud::HudDocument& document, std::string_view stem)
{
    for (int n = 1;; ++n)
    {
        std::string candidate = std::format("{}{}", stem, n);
        if (document.Find(candidate) == nullptr)
        {
            return candidate;
        }
    }
}

std::unexpected<Error> NotFound(std::string_view id)
{
    return MakeError(ErrorCode::NotFound, std::format("no widget '{}'", id));
}

} // namespace

Result<void> ValidateHud(const hud::HudDocument& document)
{
    auto parsed = hud::ParseHud(hud::WriteHud(document));
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    return {};
}

std::string AddHudWidget(UndoStack& undo, hud::HudDocument& document, hud::WidgetType type)
{
    hud::HudWidget widget;
    widget.type = type;
    widget.id = UniqueId(document, hud::ToString(type));
    switch (type)
    {
    case hud::WidgetType::Text:
        widget.text = "Text";
        break;
    case hud::WidgetType::Bar:
        widget.bind = "value";
        widget.size = {200.0f, 16.0f};
        widget.max = 100.0f;
        widget.background = {0.0f, 0.0f, 0.0f, 0.5f};
        break;
    case hud::WidgetType::Image:
        widget.image = "ui/image.png";
        widget.size = {32.0f, 32.0f};
        break;
    case hud::WidgetType::Button:
        widget.text = "Button";
        widget.action = "menu.pop";
        widget.size = {200.0f, 40.0f};
        break;
    case hud::WidgetType::List:
        widget.bind = "items";
        widget.size = {200.0f, 100.0f};
        break;
    }
    hud::HudDocument after = document;
    std::string id = widget.id;
    after.widgets.push_back(std::move(widget));
    Apply(undo, document, std::move(after), std::format("Add {}", id));
    undo.Seal();
    return id;
}

Result<void> EditHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id, hud::HudWidget edited)
{
    const std::size_t index = IndexOf(document, id);
    if (index == document.widgets.size())
    {
        return NotFound(id);
    }
    if (edited.id != id && document.Find(edited.id) != nullptr)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("widget '{}' already exists", edited.id));
    }
    hud::HudDocument after = document;
    after.widgets[index] = std::move(edited);
    INTERI_TRY(ValidateHud(after));
    Apply(undo, document, std::move(after), std::format("Edit {}", id), std::string(id));
    return {};
}

Result<void> RemoveHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id)
{
    const std::size_t index = IndexOf(document, id);
    if (index == document.widgets.size())
    {
        return NotFound(id);
    }
    hud::HudDocument after = document;
    after.widgets.erase(after.widgets.begin() + static_cast<std::ptrdiff_t>(index));
    Apply(undo, document, std::move(after), std::format("Remove {}", id));
    undo.Seal();
    return {};
}

Result<void> MoveHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id, int delta)
{
    const std::size_t index = IndexOf(document, id);
    if (index == document.widgets.size())
    {
        return NotFound(id);
    }
    const auto last = static_cast<int>(document.widgets.size()) - 1;
    const auto target = static_cast<std::size_t>(std::clamp(static_cast<int>(index) + delta, 0, last));
    if (target == index)
    {
        return {};
    }
    hud::HudDocument after = document;
    hud::HudWidget moved = std::move(after.widgets[index]);
    after.widgets.erase(after.widgets.begin() + static_cast<std::ptrdiff_t>(index));
    after.widgets.insert(after.widgets.begin() + static_cast<std::ptrdiff_t>(target), std::move(moved));
    Apply(undo, document, std::move(after), std::format("Move {}", id));
    undo.Seal();
    return {};
}

Result<std::string> DuplicateHudWidget(UndoStack& undo, hud::HudDocument& document, std::string_view id)
{
    const std::size_t index = IndexOf(document, id);
    if (index == document.widgets.size())
    {
        return NotFound(id);
    }
    hud::HudDocument after = document;
    hud::HudWidget copy = after.widgets[index];
    copy.id = UniqueId(document, id);
    std::string newId = copy.id;
    after.widgets.insert(after.widgets.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
    Apply(undo, document, std::move(after), std::format("Duplicate {}", id));
    undo.Seal();
    return newId;
}

} // namespace interi::editor
