// SPDX-License-Identifier: MIT
#pragma once

// The host service "engine.navigation": lets a module read the game's navigation mesh (the one nav.FindPath and the
// navigation agents use) without linking anything but the engine:
//
//     const auto* nav = registrar.ServiceAs<hotreload::NavigationService>(hotreload::NavigationService::kServiceName);
//     if (nav != nullptr) { if (auto mesh = nav->Source().Get()) { mesh->FindPath(a, b); } }
//
// The mesh is swapped between fixed steps only; keep the shared_ptr for the duration of a query, not longer.

#include <interi/hotreload/ModuleService.h>
#include <interi/nav/NavEvents.h>

#include <memory>
#include <string_view>

namespace interi::hotreload
{

class NavigationService final : public IModuleService
{
public:
    static constexpr std::string_view kServiceName = "engine.navigation";
    static constexpr std::string_view kServiceType = "interi.NavigationService";

    explicit NavigationService(nav::NavSource& source) noexcept
        : m_source(&source)
    {
    }

    [[nodiscard]] nav::NavSource& Source() const noexcept { return *m_source; }

    [[nodiscard]] std::string_view ServiceType() const noexcept override { return kServiceType; }
    /// Holds nothing per module.
    [[nodiscard]] std::shared_ptr<void> Extract(std::string_view) override { return nullptr; }
    void Restore(std::shared_ptr<void>) override {}

private:
    nav::NavSource* m_source;
};

} // namespace interi::hotreload
