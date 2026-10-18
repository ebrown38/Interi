// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

inline constexpr std::uint32_t kProjectFormatVersion = 1;

/// A parsed project.json: what a game is made of. Paths are relative to the project folder ('/'-separated, no
/// "..", not absolute).
///
///     {
///       "version": 1,
///       "name": "Coin Run",
///       "modules": [ "com.author.coins", "com.interi.starter" ],
///       "moduleDirs": [ "modules" ],
///       "content": [ "content" ],
///       "startupScene": "content/scenes/main.scene.json",
///       "data": "project_data.json"
///     }
///
/// "modules" is the ordered module list (top = highest priority, see ResolveModuleGraph), "moduleDirs" the folders
/// holding module folders, "content" the folders mounted for assets, "data" the project Value overrides
/// (values/ProjectData.h). "version" is required; a missing name warns; unknown keys warn; anything malformed is
/// an error.
struct Project
{
    std::string name;
    std::vector<std::string> modules;
    std::vector<std::string> moduleDirs{"modules"};
    std::vector<std::string> content{"content"};
    std::string startupScene;
    std::string data;

    std::filesystem::path directory; ///< Folder the project was loaded from (LoadProject); empty when parsed.
    std::vector<std::string> warnings;

    friend bool operator==(const Project& a, const Project& b)
    {
        return a.name == b.name && a.modules == b.modules && a.moduleDirs == b.moduleDirs && a.content == b.content &&
               a.startupScene == b.startupScene && a.data == b.data;
    }
};

inline constexpr std::string_view kProjectFileName = "project.json";

/// Parses project.json text. InvalidArgument for malformed JSON or fields, VersionMismatch for a newer version.
[[nodiscard]] Result<Project> ParseProject(std::string_view json);
/// Reads `<directory>/project.json`.
[[nodiscard]] Result<Project> LoadProject(const std::filesystem::path& directory);
/// Sorted keys, two-space indent; ParseProject reads it back equal.
[[nodiscard]] std::string WriteProject(const Project& project);
/// Writes `<project.directory>/project.json` (creating the folder). IoError if it can't.
[[nodiscard]] Result<void> SaveProject(const Project& project);

} // namespace interi::sdk
