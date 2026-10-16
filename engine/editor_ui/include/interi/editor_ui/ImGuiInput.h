// SPDX-License-Identifier: MIT
#pragma once

#include <interi/platform/InputEvents.h>

#include <imgui.h>

namespace interi::editor
{

/// ImGui's key for an engine key (ImGuiKey_None when it has none).
[[nodiscard]] ImGuiKey ToImGuiKey(Key key) noexcept;

/// Feeds platform events to ImGui's IO: keys (with Ctrl/Shift/Alt modifiers tracked across both sides), mouse
/// position, buttons and wheel, and text. Other events are ignored.
class ImGuiInput
{
public:
    void Feed(ImGuiIO& io, const InputEvent& event);

private:
    bool m_ctrl[2] = {};
    bool m_shift[2] = {};
    bool m_alt[2] = {};
};

} // namespace interi::editor
