// SPDX-License-Identifier: MIT
#pragma once

// The Module Manager window: installed modules (enabled, implicit, Physics Engines, available, incompatible) with
// priority reordering and enable/disable checked against the module graph first, the dependency graph, every item
// more than one module provides, Plugin authoring with the hot-reload loop, the trust confirmation for native code
// and the module docs page. The panel draws the models of editor/ModuleManager.h, ModuleConflicts.h and
// PluginAuthoring.h and returns what the user asked for; the app applies it.

#include <interi/editor/ModuleConflicts.h>
#include <interi/editor/ModuleManager.h>
#include <interi/editor/PluginAuthoring.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace interi::editor
{

inline constexpr const char* kModuleManagerWindow = "Module Manager";

struct ModuleAction
{
    enum class Kind : std::uint8_t
    {
        Enable,
        Disable,
        Move,
    };

    Kind kind = Kind::Enable;
    std::string id;
    std::size_t position = 0; ///< Enable and Move: the place in the project's list (0 = top).
};

enum class ModuleManagerTab : std::uint8_t
{
    Modules,
    Graph,
    Conflicts,
    Plugin,
    Docs,
};

struct ModuleManagerState
{
    bool show = false;
    std::string selected; ///< A module id.
    std::string refusal;  ///< Why the last change was refused.

    /// A change the user must confirm (it needs trust, or changes the Physics Engine).
    struct Pending
    {
        ModuleAction action;
        ModuleChange change;
    };
    std::optional<Pending> pending;

    /// Actions to run on the next draw (the conflict and palette views queue them; tests drive the panel with them).
    std::optional<ModuleAction> queued;
    bool confirm = false;
    bool cancel = false;

    // Plugin tab.
    std::string pluginId = "com.mygame.plugin";
    std::string pluginName = "My Plugin";
    std::string pluginAuthor;
    std::string pluginLicense = "MIT";
    std::string pluginFolder;
    int pluginTargetKind = 0; ///< PluginTargetKind.
    std::string pluginTarget;
    std::string pluginStatus;
    std::string buildCommand;
    bool autoReload = true;
    std::string loopOutput;
    bool loopFailed = false;

    // Docs tab.
    std::string docsModule;
    std::string docsText;

    std::optional<ModuleManagerTab> requestedTab; ///< Switches to this tab on the next draw.

    void Queue(ModuleAction action) { queued = std::move(action); }
    void ConfirmPending() noexcept { confirm = true; }
    void CancelPending() noexcept { cancel = true; }
    /// Fills the Plugin tab for a target picked in the conflict or the palette view, and shows the tab.
    void SetPluginTarget(PluginTargetKind kind, std::string id)
    {
        pluginTargetKind = static_cast<int>(kind);
        pluginTarget = std::move(id);
        pluginStatus.clear();
        requestedTab = ModuleManagerTab::Plugin;
        show = true;
    }
};

struct ModuleManagerView
{
    const ModuleManagerModel* model = nullptr;
    const DependencyGraphView* graph = nullptr;
    const ConflictReport* conflicts = nullptr;
    /// Resolves an action against the open project (EnableModule, DisableModule, MoveModule).
    std::function<Result<ModuleChange>(const ModuleAction&)> plan;
    /// Whether a build command can run (the editor has a shell and a build folder).
    bool canBuild = false;
};

struct PluginRequest
{
    PluginStubDesc desc; ///< Identity and the target's kind and id; the app describes the target.
    std::string folder;
};

struct ModuleManagerRequests
{
    /// A confirmed or direct change: save the project and reopen it.
    std::optional<ModuleChange> apply;
    /// Modules the user confirmed trusting (apply them to the TrustStore before applying the change).
    std::vector<std::string> trust;
    std::optional<PluginRequest> createPlugin;
    std::optional<std::string> generateDocs; ///< Fill `docsText` for this module.
    std::optional<std::string> saveDocs;     ///< Write the docs page for this module into the project.
    bool runLoop = false;                    ///< Build, reload and replay.
};

/// True when `change` needs the user's confirmation before it is applied.
[[nodiscard]] bool NeedsConfirmation(const ModuleChange& change) noexcept;

[[nodiscard]] ModuleManagerRequests DrawModuleManagerPanel(ModuleManagerState& state, const ModuleManagerView& view);

} // namespace interi::editor
