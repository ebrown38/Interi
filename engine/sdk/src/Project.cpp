// SPDX-License-Identifier: MIT
#include <interi/sdk/Project.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace interi::sdk
{
namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

/// A relative '/'-separated path that can't leave the project folder.
bool IsProjectPath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos)
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
        {
            return false;
        }
        start = end + 1;
    }
    return true;
}

Result<std::string> GetPath(const Json& json, std::string_view key)
{
    const auto it = json.find(key);
    if (it == json.end())
    {
        return std::string{};
    }
    if (!it->is_string() || !IsProjectPath(it->get<std::string>()))
    {
        return Invalid(std::format("project: \"{}\" must be a relative path inside the project", key));
    }
    return it->get<std::string>();
}

Result<void> GetList(const Json& json, std::string_view key, bool paths, std::vector<std::string>& out)
{
    const auto it = json.find(key);
    if (it == json.end())
    {
        return {};
    }
    if (!it->is_array())
    {
        return Invalid(std::format("project: \"{}\" must be an array", key));
    }
    out.clear();
    for (std::size_t i = 0; i < it->size(); ++i)
    {
        const Json& entry = (*it)[i];
        if (!entry.is_string() || entry.get<std::string>().empty() ||
            (paths && !IsProjectPath(entry.get<std::string>())))
        {
            return Invalid(std::format("project: \"{}\"[{}] must be a {}", key, i,
                                       paths ? "relative path inside the project" : "non-empty string"));
        }
        if (std::ranges::contains(out, entry.get<std::string>()))
        {
            return Invalid(std::format("project: \"{}\" lists '{}' twice", key, entry.get<std::string>()));
        }
        out.push_back(entry.get<std::string>());
    }
    return {};
}

} // namespace

Result<Project> ParseProject(std::string_view text)
{
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        return Invalid("project: not a JSON object");
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_unsigned())
    {
        return Invalid("project: \"version\" must be a positive integer");
    }
    if (version->get<std::uint64_t>() > kProjectFormatVersion)
    {
        return MakeError(ErrorCode::VersionMismatch, std::format("project: version {} is newer than {}",
                                                                 version->get<std::uint64_t>(), kProjectFormatVersion));
    }
    Project project;
    static constexpr std::string_view kKnown[] = {"version", "name",         "modules", "moduleDirs",
                                                  "content", "startupScene", "data",    "physicsEngine"};
    for (const auto& [key, value] : json.items())
    {
        if (!std::ranges::contains(kKnown, key))
        {
            project.warnings.push_back(std::format("project: unknown key '{}'", key));
        }
    }
    if (const auto name = json.find("name"); name == json.end())
    {
        project.warnings.emplace_back("project: missing \"name\"");
    }
    else if (!name->is_string())
    {
        return Invalid("project: \"name\" must be a string");
    }
    else
    {
        project.name = name->get<std::string>();
    }
    INTERI_TRY(GetList(json, "modules", false, project.modules));
    INTERI_TRY(GetList(json, "moduleDirs", true, project.moduleDirs));
    INTERI_TRY(GetList(json, "content", true, project.content));
    INTERI_TRY_ASSIGN(startup, GetPath(json, "startupScene"));
    project.startupScene = std::move(startup);
    INTERI_TRY_ASSIGN(data, GetPath(json, "data"));
    project.data = std::move(data);
    if (const auto pin = json.find("physicsEngine"); pin != json.end())
    {
        if (!pin->is_object() || !pin->contains("id") || !(*pin)["id"].is_string() ||
            !IsValidModuleId((*pin)["id"].get<std::string>()) ||
            (pin->contains("version") && !(*pin)["version"].is_string()))
        {
            return Invalid(R"(project: "physicsEngine" must be { "id": <module id>, "version": <range> })");
        }
        auto range = ParseVersionRange(pin->value("version", std::string{}));
        if (!range)
        {
            return Invalid(std::format("project: \"physicsEngine\": {}", range.error().message));
        }
        project.physicsEngine = PhysicsEngineRef{.id = (*pin)["id"].get<std::string>(), .version = std::move(*range)};
    }
    return project;
}

Result<Project> LoadProject(const std::filesystem::path& directory)
{
    const std::filesystem::path path = directory / kProjectFileName;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("can't read {}", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    auto project = ParseProject(text.str());
    if (!project)
    {
        return MakeError(project.error().code, std::format("{}: {}", path.string(), project.error().message));
    }
    project->directory = directory;
    return project;
}

std::string WriteProject(const Project& project)
{
    Json json = {{"version", kProjectFormatVersion},
                 {"name", project.name},
                 {"modules", project.modules},
                 {"moduleDirs", project.moduleDirs},
                 {"content", project.content}};
    if (!project.startupScene.empty())
    {
        json["startupScene"] = project.startupScene;
    }
    if (!project.data.empty())
    {
        json["data"] = project.data;
    }
    if (project.physicsEngine)
    {
        json["physicsEngine"] = {{"id", project.physicsEngine->id}, {"version", project.physicsEngine->version.Text()}};
    }
    return json.dump(2) + "\n";
}

Result<void> SaveProject(const Project& project)
{
    std::error_code error;
    std::filesystem::create_directories(project.directory, error);
    const std::filesystem::path path = project.directory / kProjectFileName;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string text = WriteProject(project);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("can't write {}", path.string()));
    }
    return {};
}

} // namespace interi::sdk
