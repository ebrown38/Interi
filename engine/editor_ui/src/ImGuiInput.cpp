// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ImGuiInput.h>

#include <variant>

namespace interi::editor
{

ImGuiKey ToImGuiKey(Key key) noexcept
{
    const auto index = static_cast<int>(key);
    if (key >= Key::A && key <= Key::Z)
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + (index - static_cast<int>(Key::A)));
    }
    if (key >= Key::Num0 && key <= Key::Num9)
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (index - static_cast<int>(Key::Num0)));
    }
    if (key >= Key::F1 && key <= Key::F12)
    {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (index - static_cast<int>(Key::F1)));
    }
    switch (key)
    {
    case Key::Space:
        return ImGuiKey_Space;
    case Key::Enter:
        return ImGuiKey_Enter;
    case Key::Escape:
        return ImGuiKey_Escape;
    case Key::Tab:
        return ImGuiKey_Tab;
    case Key::Backspace:
        return ImGuiKey_Backspace;
    case Key::Delete:
        return ImGuiKey_Delete;
    case Key::Insert:
        return ImGuiKey_Insert;
    case Key::Home:
        return ImGuiKey_Home;
    case Key::End:
        return ImGuiKey_End;
    case Key::PageUp:
        return ImGuiKey_PageUp;
    case Key::PageDown:
        return ImGuiKey_PageDown;
    case Key::Left:
        return ImGuiKey_LeftArrow;
    case Key::Right:
        return ImGuiKey_RightArrow;
    case Key::Up:
        return ImGuiKey_UpArrow;
    case Key::Down:
        return ImGuiKey_DownArrow;
    case Key::LeftShift:
        return ImGuiKey_LeftShift;
    case Key::RightShift:
        return ImGuiKey_RightShift;
    case Key::LeftCtrl:
        return ImGuiKey_LeftCtrl;
    case Key::RightCtrl:
        return ImGuiKey_RightCtrl;
    case Key::LeftAlt:
        return ImGuiKey_LeftAlt;
    case Key::RightAlt:
        return ImGuiKey_RightAlt;
    case Key::Minus:
        return ImGuiKey_Minus;
    case Key::Equals:
        return ImGuiKey_Equal;
    case Key::LeftBracket:
        return ImGuiKey_LeftBracket;
    case Key::RightBracket:
        return ImGuiKey_RightBracket;
    case Key::Backslash:
        return ImGuiKey_Backslash;
    case Key::Semicolon:
        return ImGuiKey_Semicolon;
    case Key::Apostrophe:
        return ImGuiKey_Apostrophe;
    case Key::Grave:
        return ImGuiKey_GraveAccent;
    case Key::Comma:
        return ImGuiKey_Comma;
    case Key::Period:
        return ImGuiKey_Period;
    case Key::Slash:
        return ImGuiKey_Slash;
    default:
        return ImGuiKey_None;
    }
}

void ImGuiInput::Feed(ImGuiIO& io, const InputEvent& event)
{
    if (const auto* key = std::get_if<KeyEvent>(&event))
    {
        const auto side = [&](Key left, Key right, bool (&state)[2], ImGuiKey modifier)
        {
            if (key->key == left || key->key == right)
            {
                state[key->key == left ? 0 : 1] = key->down;
                io.AddKeyEvent(modifier, state[0] || state[1]);
            }
        };
        side(Key::LeftCtrl, Key::RightCtrl, m_ctrl, ImGuiMod_Ctrl);
        side(Key::LeftShift, Key::RightShift, m_shift, ImGuiMod_Shift);
        side(Key::LeftAlt, Key::RightAlt, m_alt, ImGuiMod_Alt);
        if (const ImGuiKey mapped = ToImGuiKey(key->key); mapped != ImGuiKey_None)
        {
            io.AddKeyEvent(mapped, key->down);
        }
    }
    else if (const auto* button = std::get_if<MouseButtonEvent>(&event))
    {
        if (button->button <= MouseButton::Middle)
        {
            // Engine order Left, Right, Middle matches ImGui's 0, 1, 2.
            io.AddMouseButtonEvent(static_cast<int>(button->button), button->down);
        }
    }
    else if (const auto* motion = std::get_if<MouseMotionEvent>(&event))
    {
        io.AddMousePosEvent(motion->x, motion->y);
    }
    else if (const auto* wheel = std::get_if<MouseWheelEvent>(&event))
    {
        io.AddMouseWheelEvent(-wheel->deltaX, wheel->deltaY);
    }
    else if (const auto* text = std::get_if<TextInputEvent>(&event))
    {
        io.AddInputCharactersUTF8(text->text.data());
    }
}

} // namespace interi::editor
