// SPDX-License-Identifier: MIT
#pragma once

// Interaction for one HudDocument: pointer hover, press and release, keyboard focus. Buttons raise their action
// string; the game decides what it means (see MenuStack for the menu actions it handles itself).

#include <interi/hud/HudLayout.h>

#include <string>
#include <vector>

namespace interi::hud
{

class HudController
{
public:
    explicit HudController(HudDocument document = {})
        : m_document(std::move(document))
    {
    }

    void SetDocument(HudDocument document);
    [[nodiscard]] const HudDocument& Document() const noexcept { return m_document; }

    /// Lays the document out for the screen and the current Values. Call once per frame, before the input calls
    /// that should see the new layout.
    void Update(float width, float height, const chain::IValueReader& values);
    [[nodiscard]] const HudFrame& Frame() const noexcept { return m_frame; }

    /// Pointer in screen pixels. Moving over a button hovers and focuses it. A button activates when the pointer
    /// is released over the button it was pressed on.
    void PointerMove(float x, float y);
    void PointerDown(float x, float y);
    void PointerUp(float x, float y);

    /// Keyboard/gamepad navigation: focus moves through the visible buttons in document order (wrapping).
    void FocusNext();
    void FocusPrevious();
    /// Activates the focused button (nothing is focused: no-op).
    void Activate();

    [[nodiscard]] const std::string& Focused() const noexcept { return m_highlight.focused; }
    [[nodiscard]] const std::string& Hovered() const noexcept { return m_highlight.hovered; }

    /// Actions raised since the last call, in order.
    [[nodiscard]] std::vector<std::string> TakeActions();

private:
    [[nodiscard]] const ButtonRect* ButtonAt(float x, float y) const;
    void Raise(const ButtonRect& button);
    void Move(int step);

    HudDocument m_document;
    HudFrame m_frame;
    HudHighlight m_highlight;
    std::vector<std::string> m_actions;
    float m_width = 1280.0f;
    float m_height = 720.0f;
};

} // namespace interi::hud
