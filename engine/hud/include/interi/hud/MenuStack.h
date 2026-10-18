// SPDX-License-Identifier: MIT
#pragma once

// Main, pause and settings menus are HudDocuments (buttons raise actions) on a stack: the top one is shown and
// receives input. A button action is handled here when it is one of
//   menu.push:<name>     show another menu on top
//   menu.replace:<name>  swap the top menu
//   menu.pop             back (the last menu closing leaves the stack empty: the game resumes)
// Every other action ("game.start", "game.resume", "app.quit", "input.rebind:jump", "value.set:...") is returned to
// the game by TakeActions.

#include <interi/hud/HudController.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace interi::hud
{

inline constexpr std::string_view kMenuPush = "menu.push:";
inline constexpr std::string_view kMenuReplace = "menu.replace:";
inline constexpr std::string_view kMenuPop = "menu.pop";

class MenuStack
{
public:
    /// Registers (or replaces) a menu document under `name`.
    void Add(std::string name, HudDocument document);
    [[nodiscard]] bool Has(std::string_view name) const;

    /// False for an unknown menu (nothing changes).
    bool Push(std::string_view name);
    void Pop();
    void Clear() noexcept { m_stack.clear(); }

    [[nodiscard]] bool Empty() const noexcept { return m_stack.empty(); }
    [[nodiscard]] std::size_t Depth() const noexcept { return m_stack.size(); }
    /// Name of the top menu ("" when empty).
    [[nodiscard]] std::string_view Top() const noexcept;
    /// The top menu's controller (nullptr when empty): feed it pointer and focus input.
    [[nodiscard]] HudController* Current() noexcept;

    /// Lays the top menu out.
    void Update(float width, float height, const chain::IValueReader& values);

    /// Takes the top menu's actions, runs the menu.* ones and returns the rest, in order.
    [[nodiscard]] std::vector<std::string> TakeActions();

private:
    struct Entry
    {
        std::string name;
        std::unique_ptr<HudController> controller;
    };

    std::map<std::string, HudDocument, std::less<>> m_documents;
    std::vector<Entry> m_stack;
};

} // namespace interi::hud
