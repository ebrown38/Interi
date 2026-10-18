// SPDX-License-Identifier: MIT
#pragma once

// The open project's modules, loaded in the editor: a ModuleHost on a scratch World (the edited scene is not the
// modules' World), so the project's native chain events and functions, Values and tables exist while editing and in
// play-in-editor, and modules register editor extensions through the host services.
//
// Module systems do not run in the editor yet (play-in-editor has no shared World); events, functions, hooks, data
// and save sections do.

#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/game/ProjectModules.h>

#include <memory>
#include <string_view>

namespace interi::editor
{

class EditorModules
{
public:
    /// Loads `project`'s modules (see game::ProjectModules::Open). Fails, loading nothing, when a library is
    /// missing or doesn't load, so the editor can fall back to the core chain library.
    [[nodiscard]] static Result<std::unique_ptr<EditorModules>> Open(const sdk::Project& project,
                                                                     const game::ProjectModulesDesc& desc = {});

    [[nodiscard]] hotreload::ModuleHost& Host() noexcept { return m_modules->Host(); }
    [[nodiscard]] chain::ChainRegistry& Chains() noexcept { return m_modules->Host().Chains(); }
    [[nodiscard]] values::DataRegistry& Data() noexcept { return m_modules->Host().Data(); }
    [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept { return m_modules->Warnings(); }

    /// Hot reload: reopens module `id` (and its dependents) from its rebuilt library. Anything playing must have
    /// stopped first. The previous modules stay on failure.
    Result<hotreload::ModuleLoadStats> Reload(std::string_view id) { return m_modules->Host().Reload(id); }

private:
    EditorModules() = default;

    World m_world; ///< Declared first: destroyed last, after the host.
    std::unique_ptr<game::ProjectModules> m_modules;
};

} // namespace interi::editor
