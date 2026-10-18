// SPDX-License-Identifier: MIT
#pragma once

// Projects on disk: creating one (empty, or from a module's template) and finding its modules.

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Project.h>

#include <filesystem>
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

} // namespace interi::editor
