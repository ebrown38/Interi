// SPDX-License-Identifier: MIT
#include <interi/hud/MenuStack.h>

namespace interi::hud
{

void MenuStack::Add(std::string name, HudDocument document)
{
    m_documents.insert_or_assign(std::move(name), std::move(document));
}

bool MenuStack::Has(std::string_view name) const
{
    return m_documents.contains(name);
}

bool MenuStack::Push(std::string_view name)
{
    const auto found = m_documents.find(name);
    if (found == m_documents.end())
    {
        return false;
    }
    m_stack.push_back({.name = found->first, .controller = std::make_unique<HudController>(found->second)});
    return true;
}

void MenuStack::Pop()
{
    if (!m_stack.empty())
    {
        m_stack.pop_back();
    }
}

std::string_view MenuStack::Top() const noexcept
{
    return m_stack.empty() ? std::string_view() : std::string_view(m_stack.back().name);
}

HudController* MenuStack::Current() noexcept
{
    return m_stack.empty() ? nullptr : m_stack.back().controller.get();
}

void MenuStack::Update(float width, float height, const chain::IValueReader& values)
{
    if (HudController* top = Current())
    {
        top->Update(width, height, values);
    }
}

std::vector<std::string> MenuStack::TakeActions()
{
    std::vector<std::string> unhandled;
    HudController* top = Current();
    if (top == nullptr)
    {
        return unhandled;
    }
    for (std::string& action : top->TakeActions())
    {
        if (action == kMenuPop)
        {
            Pop();
        }
        else if (action.starts_with(kMenuPush))
        {
            (void)Push(std::string_view(action).substr(kMenuPush.size()));
        }
        else if (action.starts_with(kMenuReplace))
        {
            // Unknown target: keep the current menu rather than closing it.
            const std::string_view target = std::string_view(action).substr(kMenuReplace.size());
            if (Has(target))
            {
                Pop();
                (void)Push(target);
            }
        }
        else
        {
            unhandled.push_back(std::move(action));
        }
    }
    return unhandled;
}

} // namespace interi::hud
