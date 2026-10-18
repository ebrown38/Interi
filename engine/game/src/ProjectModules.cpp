// SPDX-License-Identifier: MIT
#include <interi/game/ProjectModules.h>
#include <interi/platform/FileSystem.h>
#include <interi/values/ProjectData.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace interi::game
{

Result<std::vector<sdk::ModuleManifest>> LoadProjectManifests(const sdk::Project& project)
{
    std::vector<sdk::ModuleManifest> manifests;
    for (const std::string& folder : project.moduleDirs)
    {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(project.directory / folder, error))
        {
            if (!std::filesystem::exists(entry.path() / "module.json"))
            {
                continue;
            }
            INTERI_TRY_ASSIGN(manifest, sdk::LoadManifest(entry.path()));
            manifests.push_back(std::move(manifest));
        }
    }
    std::ranges::sort(manifests, {}, &sdk::ModuleManifest::id);
    return manifests;
}

Result<std::unique_ptr<ProjectModules>> ProjectModules::Open(const sdk::Project& project, World& world,
                                                             const ProjectModulesDesc& desc)
{
    std::unique_ptr<ProjectModules> modules(new ProjectModules());
    INTERI_TRY_ASSIGN(manifests, LoadProjectManifests(project));
    modules->m_manifests = std::move(manifests);

    hotreload::ModuleHostDesc hostDesc;
    hostDesc.librarySearchPaths = desc.librarySearchPaths;
    hostDesc.shadowDirectory = desc.shadowDirectory;
    hostDesc.services = desc.services;
    modules->m_host = std::make_unique<hotreload::ModuleHost>(world, std::move(hostDesc));
    if (!project.modules.empty())
    {
        INTERI_TRY(modules->m_host->LoadProject(project.modules, modules->m_manifests, project.physicsEngine));
    }

    if (!project.data.empty())
    {
        std::ifstream file(project.directory / project.data, std::ios::binary);
        if (file.is_open())
        {
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto warnings = values::LoadProjectData(text, modules->m_host->Data());
            if (!warnings)
            {
                return MakeError(warnings.error().code, std::format("{}: {}", project.data, warnings.error().message));
            }
            modules->m_warnings = std::move(*warnings);
        }
    }
    return modules;
}

} // namespace interi::game
