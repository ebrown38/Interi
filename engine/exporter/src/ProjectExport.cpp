// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/exporter/ProjectExport.h>
#include <interi/game/ProjectModules.h>
#include <interi/scene/SceneFile.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/sdk/Project.h>
#include <interi/sdk/Toolchain.h>
#include <interi/values/DataRegistry.h>
#include <interi/values/ModuleContent.h>
#include <interi/values/ProjectData.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace interi::exporter
{

namespace fs = std::filesystem;

namespace
{

Result<std::vector<std::byte>> ReadBytes(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return MakeError(ErrorCode::NotFound, std::format("'{}' can't be read", path.string()));
    }
    const std::vector<char> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(text.size());
    std::ranges::transform(text, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

Result<void> WriteText(const fs::path& path, std::string_view text)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good())
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' can't be written", path.string()));
    }
    return {};
}

Result<void> CopyTree(const fs::path& from, const fs::path& to)
{
    std::error_code error;
    fs::create_directories(to, error);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("copying '{}' to '{}': {}", from.string(), to.string(), error.message()));
    }
    return {};
}

std::vector<fs::path> LibraryFileNames(std::string_view base)
{
#if defined(_WIN32)
    return {std::format("{}.dll", base), std::format("lib{}.dll", base)};
#else
    return {std::format("lib{}.so", base), std::format("{}.so", base)};
#endif
}

// A module library: next to module.json, else in a search path, copied into the module's folder in the package.
Result<void> PackageLibrary(const sdk::ModuleManifest& manifest, const std::string& library,
                            const std::vector<fs::path>& searchPaths, const fs::path& destinationFolder)
{
    if (library.empty())
    {
        return {};
    }
    std::vector<fs::path> directories{manifest.directory};
    directories.insert(directories.end(), searchPaths.begin(), searchPaths.end());
    for (const fs::path& directory : directories)
    {
        for (const fs::path& name : LibraryFileNames(library))
        {
            std::error_code error;
            if (fs::is_regular_file(directory / name, error))
            {
                fs::create_directories(destinationFolder, error);
                fs::copy_file(directory / name, destinationFolder / name, fs::copy_options::overwrite_existing, error);
                if (error)
                {
                    return MakeError(ErrorCode::IoError,
                                     std::format("copying '{}': {}", name.string(), error.message()));
                }
                return {};
            }
        }
    }
    return MakeError(ErrorCode::NotFound, std::format("module '{}': library '{}' was not found", manifest.id, library));
}

std::string Slug(const std::string& name)
{
    std::string slug;
    for (const char c : name)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
        {
            slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        else if (!slug.empty() && slug.back() != '_')
        {
            slug += '_';
        }
    }
    while (!slug.empty() && slug.back() == '_')
    {
        slug.pop_back();
    }
    return slug.empty() ? "game" : slug;
}

std::string HexHash(std::uint64_t hash)
{
    return std::format("{:016x}", hash);
}

// Cooks the project's resolved data from the manifests alone (no libraries): modules' Values, archetypes and tables
// with the project's data file on top.
Result<std::vector<std::byte>> CookProjectData(const sdk::Project& project, const sdk::ModuleGraph& graph,
                                               std::vector<std::string>& warnings)
{
    const sdk::ModuleSet modules(graph, std::span<const std::string>{});
    values::DataRegistry data(modules);
    for (const std::size_t index : graph.loadOrder)
    {
        INTERI_TRY_ASSIGN(content, values::LoadModuleContent(graph.modules[index].manifest));
        INTERI_TRY(data.AddModule(content));
    }
    if (!project.data.empty())
    {
        auto bytes = ReadBytes(project.directory / project.data);
        if (bytes)
        {
            const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            INTERI_TRY_ASSIGN(loaded, values::LoadProjectData(text, data));
            warnings.insert(warnings.end(), loaded.begin(), loaded.end());
        }
    }
    return values::WriteCookedData(values::CookData(data, &warnings));
}

Result<std::vector<PackagedFile>> ListFiles(const fs::path& root)
{
    std::vector<PackagedFile> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file())
        {
            continue;
        }
        const std::string relative = fs::relative(it->path(), root).generic_string();
        if (relative == kPackageFileName)
        {
            continue;
        }
        INTERI_TRY_ASSIGN(bytes, ReadBytes(it->path()));
        files.push_back({relative, bytes.size(), HashBytes(bytes.data(), bytes.size())});
    }
    if (error)
    {
        return MakeError(ErrorCode::IoError, error.message());
    }
    std::ranges::sort(files, {}, &PackagedFile::path);
    return files;
}

bool SafeRelative(const std::string& path)
{
    const fs::path p(path);
    if (path.empty() || p.is_absolute() || p.has_root_name() || path.find(':') != std::string::npos ||
        path.find('\\') != std::string::npos)
    {
        return false;
    }
    return std::ranges::none_of(p, [](const fs::path& part) { return part == ".."; });
}

Result<ExportReport> Stage(const ExportDesc& desc, const fs::path& stage)
{
    ExportReport report;
    INTERI_TRY_ASSIGN(project, sdk::LoadProject(desc.projectDirectory));
    report.gameName = project.name;
    report.warnings = project.warnings;
    INTERI_TRY_ASSIGN(manifests, game::LoadProjectManifests(project));
    INTERI_TRY_ASSIGN(graph,
                      sdk::ResolveModuleGraph(project.modules, manifests, sdk::CurrentHost(), project.physicsEngine));

    const fs::path gameRoot = stage / kGameFolder;
    // Content folders, the data file and the project itself.
    for (const std::string& folder : project.content)
    {
        std::error_code error;
        if (fs::is_directory(project.directory / folder, error))
        {
            INTERI_TRY(CopyTree(project.directory / folder, gameRoot / folder));
        }
    }
    if (!project.data.empty() && fs::is_regular_file(project.directory / project.data))
    {
        std::error_code error;
        fs::create_directories((gameRoot / project.data).parent_path(), error);
        fs::copy_file(project.directory / project.data, gameRoot / project.data, error);
        if (error)
        {
            return MakeError(ErrorCode::IoError, std::format("copying '{}': {}", project.data, error.message()));
        }
    }
    INTERI_TRY(sdk::SaveProject(
        [&]
        {
            sdk::Project copy = project;
            copy.directory = gameRoot;
            return copy;
        }()));

    // The startup scene must be in the package and parse.
    if (!project.startupScene.empty())
    {
        auto scene = ReadBytes(gameRoot / project.startupScene);
        if (!scene)
        {
            return MakeError(
                ErrorCode::NotFound,
                std::format("the startup scene '{}' is not in the project's content folders", project.startupScene));
        }
        INTERI_TRY(scene::ParseScene(std::string_view(reinterpret_cast<const char*>(scene->data()), scene->size())));
    }

    // Modules: every module of the resolved graph, with its library.
    for (const std::size_t index : graph.loadOrder)
    {
        const sdk::ModuleManifest& manifest = graph.modules[index].manifest;
        if (manifest.directory.empty())
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("module '{}' has no folder in the project's module directories", manifest.id));
        }
        const fs::path destination = gameRoot / fs::relative(manifest.directory, project.directory);
        INTERI_TRY(CopyTree(manifest.directory, destination));
        INTERI_TRY(PackageLibrary(manifest, manifest.library, desc.librarySearchPaths, destination));
        for (const sdk::PhysicsEngineEntry& entry : manifest.physicsEngines)
        {
            INTERI_TRY(PackageLibrary(manifest, entry.library, desc.librarySearchPaths, destination));
        }
        report.modules.push_back(manifest.id);
    }

    INTERI_TRY_ASSIGN(cooked, CookProjectData(project, graph, report.warnings));
    {
        std::ofstream file(gameRoot / "cooked.ival", std::ios::binary);
        file.write(reinterpret_cast<const char*>(cooked.data()), static_cast<std::streamsize>(cooked.size()));
        if (!file.good())
        {
            return MakeError(ErrorCode::IoError, "cooked.ival can't be written");
        }
    }

    // The runtime and the launchers.
    for (const fs::path& source : desc.runtimeFiles)
    {
        std::error_code error;
        const fs::path binary = stage / kRuntimeFolder;
        fs::create_directories(binary, error);
        if (fs::is_directory(source, error))
        {
            for (const auto& entry : fs::directory_iterator(source, error))
            {
                if (entry.is_regular_file())
                {
                    fs::copy_file(entry.path(), binary / entry.path().filename(), fs::copy_options::overwrite_existing,
                                  error);
                }
            }
        }
        else
        {
            fs::copy_file(source, binary / source.filename(), fs::copy_options::overwrite_existing, error);
        }
        if (error)
        {
            return MakeError(ErrorCode::IoError, std::format("copying '{}': {}", source.string(), error.message()));
        }
    }
    const std::string slug = Slug(project.name);
    INTERI_TRY(WriteText(stage / (slug + ".sh"),
                         std::format("#!/bin/sh\nhere=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
                                     "LD_LIBRARY_PATH=\"$here/{0}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}\" "
                                     "exec \"$here/{0}/{1}\" --package \"$here\" \"$@\"\n",
                                     kRuntimeFolder, desc.playerName)));
    {
        std::error_code error;
        fs::permissions(stage / (slug + ".sh"), fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, error);
    }
    INTERI_TRY(
        WriteText(stage / (slug + ".bat"),
                  std::format("@echo off\r\nset \"HERE=%~dp0\"\r\n\"%HERE%{}\\{}.exe\" --package \"%HERE%.\" %*\r\n",
                              kRuntimeFolder, desc.playerName)));

    INTERI_TRY_ASSIGN(files, ListFiles(stage));
    report.files = std::move(files);

    nlohmann::json manifest;
    manifest["format"] = kPackageFormatVersion;
    manifest["name"] = project.name;
    manifest["engine"] = sdk::CurrentHost().engineVersion.ToString();
    manifest["toolchain"] = std::string(sdk::EngineToolchainFingerprint());
    nlohmann::json moduleList = nlohmann::json::array();
    for (const std::size_t index : graph.loadOrder)
    {
        const sdk::ModuleManifest& module = graph.modules[index].manifest;
        moduleList.push_back(
            {{"id", module.id},
             {"path", (fs::path(kGameFolder) / fs::relative(module.directory, project.directory)).generic_string()}});
    }
    manifest["modules"] = std::move(moduleList);
    nlohmann::json fileList = nlohmann::json::array();
    for (const PackagedFile& file : report.files)
    {
        fileList.push_back({{"path", file.path}, {"size", file.size}, {"hash", HexHash(file.hash)}});
    }
    manifest["files"] = std::move(fileList);
    INTERI_TRY(WriteText(stage / kPackageFileName, manifest.dump(2) + "\n"));
    return report;
}

} // namespace

Result<ExportReport> ExportProject(const ExportDesc& desc)
{
    std::error_code error;
    if (fs::exists(desc.outputDirectory, error) && !fs::is_empty(desc.outputDirectory, error))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("the output folder '{}' is not empty", desc.outputDirectory.string()));
    }
    const fs::path stage = desc.outputDirectory.string() + ".partial";
    fs::remove_all(stage, error);
    fs::create_directories(stage, error);
    if (error)
    {
        return MakeError(ErrorCode::IoError, std::format("'{}': {}", stage.string(), error.message()));
    }
    auto report = Stage(desc, stage);
    if (!report)
    {
        fs::remove_all(stage, error);
        return report;
    }
    fs::remove_all(desc.outputDirectory, error);
    fs::rename(stage, desc.outputDirectory, error);
    if (error)
    {
        fs::remove_all(stage, error);
        return MakeError(ErrorCode::IoError, std::format("'{}': {}", desc.outputDirectory.string(), error.message()));
    }
    return report;
}

Result<PackageInfo> VerifyPackage(const fs::path& root)
{
    INTERI_TRY_ASSIGN(bytes, ReadBytes(root / kPackageFileName));
    const nlohmann::json manifest = nlohmann::json::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), nullptr, false);
    if (!manifest.is_object() || !manifest.contains("format") || !manifest["format"].is_number_unsigned() ||
        !manifest.contains("files") || !manifest["files"].is_array() || !manifest.contains("modules") ||
        !manifest["modules"].is_array() || manifest.value("toolchain", std::string{}).empty())
    {
        return MakeError(ErrorCode::Corrupt, "package.json is malformed");
    }
    if (manifest["format"].get<std::uint32_t>() > kPackageFormatVersion)
    {
        return MakeError(ErrorCode::VersionMismatch, "the package was written by a newer engine");
    }
    if (manifest["toolchain"].get<std::string>() != sdk::EngineToolchainFingerprint())
    {
        return MakeError(ErrorCode::VersionMismatch, "the package was built with another toolchain than this player");
    }

    PackageInfo info;
    info.root = root;
    info.gameDirectory = root / kGameFolder;
    info.gameName = manifest.value("name", std::string{});
    info.engineVersion = manifest.value("engine", std::string{});
    for (const nlohmann::json& file : manifest["files"])
    {
        if (!file.is_object() || file.value("path", std::string{}).empty() || !file.contains("size") ||
            !file["size"].is_number_unsigned() || file.value("hash", std::string{}).empty())
        {
            return MakeError(ErrorCode::Corrupt, "package.json lists a malformed file");
        }
        const std::string path = file["path"].get<std::string>();
        if (!SafeRelative(path))
        {
            return MakeError(ErrorCode::Corrupt, std::format("package.json lists '{}', outside the package", path));
        }
        auto data = ReadBytes(root / path);
        if (!data)
        {
            return MakeError(ErrorCode::Corrupt, std::format("'{}' is missing", path));
        }
        if (data->size() != file["size"].get<std::uint64_t>() ||
            HexHash(HashBytes(data->data(), data->size())) != file["hash"].get<std::string>())
        {
            return MakeError(ErrorCode::Corrupt, std::format("'{}' was changed or is damaged", path));
        }
        ++info.fileCount;
    }
    for (const nlohmann::json& module : manifest["modules"])
    {
        const std::string id = module.is_object() ? module.value("id", std::string{}) : std::string{};
        const std::string path = module.is_object() ? module.value("path", std::string{}) : std::string{};
        if (id.empty() || !SafeRelative(path))
        {
            return MakeError(ErrorCode::Corrupt, "package.json lists a malformed module");
        }
        info.modules.push_back(id);
        info.libraryDirectories.push_back(root / path);
    }
    return info;
}

} // namespace interi::exporter
