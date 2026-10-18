// SPDX-License-Identifier: MIT
#pragma once

// Projects on disk: creating one (empty, or from a module's template) and finding its modules.

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/sdk/Project.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace interi::editor
{

/// A starter project a module ships (its manifest's "templates" section).
struct ProjectTemplate
{
    std::string module; ///< The module's id.
    std::string name;
    std::string description;
    std::filesystem::path directory; ///< The template's files.
};

/// Every template the manifests declare, in manifest then declaration order.
[[nodiscard]] std::vector<ProjectTemplate> ListTemplates(std::span<const sdk::ModuleManifest> manifests);

inline constexpr std::string_view kDefaultStartupScene = "content/scenes/main.scene.json";

/// Creates a project in `directory` (created; it must not exist or be empty). From a template, its files are
/// copied and its project.json (if any) gets `name`; without one the project lists the template's module. Without
/// a template: project.json (no modules, content folder "content"), and an empty startup scene. Returns the
/// project as written.
[[nodiscard]] Result<sdk::Project> CreateProject(const std::filesystem::path& directory, std::string name,
                                                 const ProjectTemplate* from = nullptr);

/// The manifests of every module folder (a folder holding module.json) in the project's module dirs, by id.
[[nodiscard]] Result<std::vector<sdk::ModuleManifest>> LoadProjectModules(const sdk::Project& project);

/// The project's Physics Engine as the settings show it.
struct PhysicsEngineStatus
{
    bool selected = false;
    bool pinned = false;
    std::string id;
    std::string version;
    /// Why: "pinned by the project", "default of <module>" or "fallback of <module>: its default isn't supported by
    /// every module".
    std::string reason;
    /// The engines the pin may name: every installed engine the project's modules all support (the selected one
    /// included).
    std::vector<std::string> pinnable;
};

/// Describes the engine `graph` selected (resolved from `project` and `manifests`) and the engines that could be
/// pinned instead.
[[nodiscard]] PhysicsEngineStatus DescribePhysicsEngine(const sdk::ModuleGraph& graph, const sdk::Project& project,
                                                        std::span<const sdk::ModuleManifest> manifests,
                                                        const sdk::HostInfo& host);

/// `project` with `candidate` added at the top of its module order, if the project still resolves (sdk::CanAddModule);
/// otherwise the reason it is refused (log it and show it in the Project menu). `project` is not changed.
[[nodiscard]] Result<sdk::Project> AddProjectModule(const sdk::Project& project,
                                                    std::span<const sdk::ModuleManifest> manifests,
                                                    const sdk::ModuleManifest& candidate, const sdk::HostInfo& host);

/// `project` with its Physics Engine pin set to `pin` (empty: cleared), if the project still resolves; otherwise the
/// reason. `project` is not changed.
[[nodiscard]] Result<sdk::Project> PinProjectPhysicsEngine(const sdk::Project& project,
                                                           std::span<const sdk::ModuleManifest> manifests,
                                                           const std::optional<sdk::PhysicsEngineRef>& pin,
                                                           const sdk::HostInfo& host);

} // namespace interi::editor
