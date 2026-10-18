// SPDX-License-Identifier: MIT
#include <interi/hud/HudController.h>

#include <algorithm>
#include <ranges>
#include <utility>

namespace interi::hud
{

void HudController::SetDocument(HudDocument document)
{
    m_document = std::move(document);
    m_frame = {};
    m_highlight = {};
}

void HudController::Update(float width, float height, const chain::IValueReader& values)
{
    m_width = width;
    m_height = height;
    m_frame = BuildFrame(m_document, width, height, values, m_highlight);
    // A button that disappeared (visibleIf) can't be focused, hovered or pressed any more.
    for (std::string* id : {&m_highlight.focused, &m_highlight.hovered, &m_highlight.pressed})
    {
        if (!id->empty() && std::ranges::none_of(m_frame.buttons, [&](const ButtonRect& b) { return b.widget == *id; }))
        {
            id->clear();
        }
    }
}

const ButtonRect* HudController::ButtonAt(float x, float y) const
{
    // Later widgets draw on top: the last match wins.
    for (const ButtonRect& button : std::views::reverse(m_frame.buttons))
    {
        if (button.rect.Contains(x, y))
        {
            return &button;
        }
    }
    return nullptr;
}

void HudController::Raise(const ButtonRect& button)
{
    m_actions.push_back(button.action);
}

void HudController::PointerMove(float x, float y)
{
    const ButtonRect* button = ButtonAt(x, y);
    m_highlight.hovered = button == nullptr ? std::string() : button->widget;
    if (button != nullptr)
    {
        m_highlight.focused = button->widget;
    }
}

void HudController::PointerDown(float x, float y)
{
    PointerMove(x, y);
    m_highlight.pressed = m_highlight.hovered;
}

void HudController::PointerUp(float x, float y)
{
    PointerMove(x, y);
    if (!m_highlight.pressed.empty() && m_highlight.pressed == m_highlight.hovered)
    {
        if (const ButtonRect* button = ButtonAt(x, y))
        {
            Raise(*button);
        }
    }
    m_highlight.pressed.clear();
}

void HudController::Move(int step)
{
    const auto count = static_cast<int>(m_frame.buttons.size());
    if (count == 0)
    {
        return;
    }
    const auto current =
        std::ranges::find_if(m_frame.buttons, [&](const ButtonRect& b) { return b.widget == m_highlight.focused; });
    int index =
        current == m_frame.buttons.end() ? (step > 0 ? -1 : 0) : static_cast<int>(current - m_frame.buttons.begin());
    index = ((index + step) % count + count) % count;
    m_highlight.focused = m_frame.buttons[static_cast<std::size_t>(index)].widget;
}

void HudController::FocusNext()
{
    Move(1);
}

void HudController::FocusPrevious()
{
    Move(-1);
}

void HudController::Activate()
{
    for (const ButtonRect& button : m_frame.buttons)
    {
        if (button.widget == m_highlight.focused)
        {
            Raise(button);
            return;
        }
    }
}

std::vector<std::string> HudController::TakeActions()
{
    return std::exchange(m_actions, {});
}

} // namespace interi::hud
