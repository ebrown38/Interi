// SPDX-License-Identifier: MIT
#pragma once

// Game export. ExportProject cooks a project (project.json, its modules, content, data) into a package folder:
//
//     <output>/package.json           format version, game name, engine version, toolchain, every file's size and hash
//     <output>/<name>.sh, <name>.bat  launchers: run bin/interi_player on the game folder
//     <output>/bin/                   the runtime: the player and the engine libraries (ExportDesc::runtimeFiles)
//     <output>/game/project.json      the project, as a GameSession opens it
//     <output>/game/<content>/...     the project's content folders and data file
//     <output>/game/<moduleDir>/<id>/ each module of the resolved graph: module.json, its content and its library
//     <output>/game/cooked.ival       the resolved Values, archetypes and tables (values/ProjectData.h)
//
// Exporting is all or nothing: the project must resolve, its data must fit, the startup scene must parse and every
// module library must be found; otherwise the output folder is left as it was. VerifyPackage checks a package on the
// machine that runs it (every file's size and hash, the toolchain the libraries were built with).

#include <interi/core/Result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace interi::exporter
{

inline constexpr std::uint32_t kPackageFormatVersion = 1;
inline constexpr const char* kPackageFileName = "package.json";
inline constexpr const char* kGameFolder = "game";
inline constexpr const char* kRuntimeFolder = "bin";

struct ExportDesc
{
    std::filesystem::path projectDirectory;
    /// Must not exist or must be empty.
    std::filesystem::path outputDirectory;
    /// Where to look for a module's library when it isn't in the module's folder (a build output directory).
    std::vector<std::filesystem::path> librarySearchPaths;
    /// Copied into `bin/`: the player executable and the engine libraries it needs. Directories are copied by their
    /// regular files, one level deep.
    std::vector<std::filesystem::path> runtimeFiles;
    /// The file name of the player inside `bin/` the launchers run.
    std::string playerName = "interi_player";
};

struct PackagedFile
{
    std::string path; ///< Relative to the package, '/'-separated.
    std::uint64_t size = 0;
    std::uint64_t hash = 0;
};

struct ExportReport
{
    std::string gameName;
    std::vector<std::string> modules; ///< The modules of the resolved graph, in load order.
    std::vector<PackagedFile> files;  ///< Sorted by path; package.json itself is not listed.
    std::vector<std::string> warnings;
};

[[nodiscard]] Result<ExportReport> ExportProject(const ExportDesc& desc);

struct PackageInfo
{
    std::string gameName;
    std::string engineVersion;
    std::filesystem::path root;
    std::filesystem::path gameDirectory;                   ///< What GameDesc::projectDirectory takes.
    std::vector<std::filesystem::path> libraryDirectories; ///< The module folders (library search paths).
    std::vector<std::string> modules;
    std::size_t fileCount = 0;
};

/// Reads `<root>/package.json`, checks that every listed file exists with its size and hash, and that the toolchain
/// the package was built with is this one. NotFound, Corrupt (a missing, changed or truncated file; a path that
/// escapes the package) or VersionMismatch (a newer format, another toolchain).
[[nodiscard]] Result<PackageInfo> VerifyPackage(const std::filesystem::path& root);

} // namespace interi::exporter
