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

PhysicsEngineStatus DescribePhysicsEngine(const sdk::ModuleGraph& graph, const sdk::Project& project,
                                          std::span<const sdk::ModuleManifest> manifests, const sdk::HostInfo& host)
{
    PhysicsEngineStatus status;
    if (graph.physicsEngine != sdk::ModuleGraph::npos)
    {
        const sdk::ModuleManifest& engine = graph.modules[graph.physicsEngine].manifest;
        status.selected = true;
        status.pinned = graph.physicsEngineReason == sdk::PhysicsEngineReason::Pinned;
        status.id = engine.id;
        status.version = engine.version.ToString();
        switch (graph.physicsEngineReason)
        {
        case sdk::PhysicsEngineReason::Pinned:
            status.reason = "pinned by the project";
            break;
        case sdk::PhysicsEngineReason::Default:
            status.reason = std::format("default of {}", graph.physicsEngineChosenBy);
            break;
        case sdk::PhysicsEngineReason::Fallback:
            status.reason =
                std::format("fallback of {}: its default isn't supported by every module", graph.physicsEngineChosenBy);
            break;
        case sdk::PhysicsEngineReason::None:
            break;
        }
    }
    std::vector<std::string> ids;
    for (const sdk::ModuleManifest& manifest : manifests)
    {
        if (manifest.kind == sdk::ModuleKind::PhysicsEngine && !std::ranges::contains(ids, manifest.id))
        {
            ids.push_back(manifest.id);
        }
    }
    std::ranges::sort(ids);
    for (const std::string& id : ids)
    {
        const std::optional<sdk::PhysicsEngineRef> pin = sdk::PhysicsEngineRef{.id = id, .version = {}};
        if (sdk::ResolveModuleGraph(project.modules, manifests, host, pin))
        {
            status.pinnable.push_back(id);
        }
    }
    return status;
}

Result<sdk::Project> AddProjectModule(const sdk::Project& project, std::span<const sdk::ModuleManifest> manifests,
                                      const sdk::ModuleManifest& candidate, const sdk::HostInfo& host)
{
    INTERI_TRY(sdk::CanAddModule(project, manifests, candidate, host));
    sdk::Project result = project;
    result.modules.insert(result.modules.begin(), candidate.id);
    return result;
}

Result<sdk::Project> PinProjectPhysicsEngine(const sdk::Project& project,
                                             std::span<const sdk::ModuleManifest> manifests,
                                             const std::optional<sdk::PhysicsEngineRef>& pin, const sdk::HostInfo& host)
{
    INTERI_TRY_ASSIGN(graph, sdk::ResolveModuleGraph(project.modules, manifests, host, pin));
    sdk::Project result = project;
    result.physicsEngine = pin;
    return result;
}

} // namespace interi::editor
