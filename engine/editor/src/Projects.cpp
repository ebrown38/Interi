// SPDX-License-Identifier: MIT
#include <interi/editor/Projects.h>
#include <interi/scene/SceneFile.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace interi::editor
{

std::vector<ProjectTemplate> ListTemplates(std::span<const sdk::ModuleManifest> manifests)
{
    std::vector<ProjectTemplate> templates;
    for (const sdk::ModuleManifest& manifest : manifests)
    {
        for (const sdk::ModuleTemplate& entry : manifest.templates)
        {
            templates.push_back({.module = manifest.id,
                                 .name = entry.name,
                                 .description = entry.description,
                                 .directory = manifest.directory / entry.path});
        }
    }
    return templates;
}

Result<sdk::Project> CreateProject(const std::filesystem::path& directory, std::string name,
                                   const ProjectTemplate* from)
{
    std::error_code error;
    if (std::filesystem::exists(directory, error) && !std::filesystem::is_empty(directory, error))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("{} exists and is not empty", directory.string()));
    }
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return MakeError(ErrorCode::IoError, std::format("can't create {}: {}", directory.string(), error.message()));
    }
    sdk::Project project;
    if (from != nullptr)
    {
        std::filesystem::copy(from->directory, directory, std::filesystem::copy_options::recursive, error);
        if (error)
        {
            return MakeError(ErrorCode::IoError,
                             std::format("can't copy template {}: {}", from->directory.string(), error.message()));
        }
        if (std::filesystem::exists(directory / sdk::kProjectFileName))
        {
            INTERI_TRY_ASSIGN(loaded, sdk::LoadProject(directory));
            project = std::move(loaded);
        }
        else
        {
            project.modules = {from->module};
        }
    }
    else
    {
        project.startupScene = std::string(kDefaultStartupScene);
        const std::filesystem::path scene = directory / kDefaultStartupScene;
        std::filesystem::create_directories(scene.parent_path(), error);
        std::ofstream file(scene, std::ios::binary);
        const std::string text = scene::WriteScene({});
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file)
        {
            return MakeError(ErrorCode::IoError, std::format("can't write {}", scene.string()));
        }
    }
    project.name = std::move(name);
    project.directory = directory;
    project.warnings.clear();
    for (const std::string& content : project.content)
    {
        std::filesystem::create_directories(directory / content, error);
    }
    INTERI_TRY(sdk::SaveProject(project));
    return project;
}

Result<std::vector<sdk::ModuleManifest>> LoadProjectModules(const sdk::Project& project)
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

} // namespace interi::editor
