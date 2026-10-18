// SPDX-License-Identifier: MIT
#pragma once

// A project's modules, loaded: the manifests of the project's module folders, a ModuleHost running them against a
// World, and the project's data file applied on top of the modules' Values, attributes and tables.

#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Project.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace interi::game
{

/// The manifests of every module folder (a folder holding module.json) in the project's module dirs, by id.
[[nodiscard]] Result<std::vector<sdk::ModuleManifest>> LoadProjectManifests(const sdk::Project& project);

struct ProjectModulesDesc
{
    /// Searched for a manifest's library after its own folder (a build output directory).
    std::vector<std::filesystem::path> librarySearchPaths;
    /// Where the host copies libraries before opening them (empty: under the temp directory).
    std::filesystem::path shadowDirectory;
    /// Host services modules can reach (ModuleHostDesc::services); they must outlive the ProjectModules.
    std::map<std::string, hotreload::IModuleService*, std::less<>> services;
};

class ProjectModules
{
public:
    /// Loads `project`'s modules into a host on `world` (which must outlive this) and applies the project's data
    /// file (`project.data`, relative to `project.directory`; a missing file is fine). All or nothing: nothing is
    /// loaded when the module graph doesn't resolve, a library doesn't load, or the project data doesn't fit.
    [[nodiscard]] static Result<std::unique_ptr<ProjectModules>> Open(const sdk::Project& project, World& world,
                                                                      const ProjectModulesDesc& desc = {});

    [[nodiscard]] hotreload::ModuleHost& Host() noexcept { return *m_host; }
    [[nodiscard]] const std::vector<sdk::ModuleManifest>& Manifests() const noexcept { return m_manifests; }
    /// Migration warnings from loading the project data.
    [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept { return m_warnings; }

private:
    ProjectModules() = default;

    std::vector<sdk::ModuleManifest> m_manifests;
    std::unique_ptr<hotreload::ModuleHost> m_host;
    std::vector<std::string> m_warnings;
};

} // namespace interi::game
