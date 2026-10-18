// SPDX-License-Identifier: MIT
#pragma once

// Editor service panels: the log console, preferences (with the profiler toggle) and the project menu.

#include <interi/editor/EditorServices.h>
#include <interi/editor/Projects.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace interi::editor
{

inline constexpr const char* kConsoleWindow = "Console";
inline constexpr const char* kPreferencesWindow = "Preferences";

struct ConsoleState
{
    int minimumLevel = 0; ///< LogLevel index.
    std::string filter;
    bool autoScroll = true;
};

/// The log, filtered by level and text (a category or message substring).
void DrawConsolePanel(LogBuffer& log, ConsoleState& state);

/// Preferences: autosave, play, snapping and the Tracy frame marks (shown as unavailable unless the build has
/// INTERI_PROFILING). Returns true when something changed (save them).
[[nodiscard]] bool DrawPreferencesPanel(Preferences& preferences, bool profilerBuild, bool profilerConnected);

struct ProjectMenuState
{
    std::string newName = "New project";
    std::string newFolder;
    int templateIndex = -1; ///< -1: empty project.
    std::string openFolder;
    bool showNew = false;
    bool showOpen = false;
};

struct ProjectRequests
{
    std::optional<std::string> open; ///< A project folder.
    std::optional<std::string> createFolder;
    std::string createName;
    int createTemplate = -1;
};

/// The main menu bar's Project menu (it adds itself to the bar) with New (name, folder, template), Open (folder)
/// and the recent projects.
[[nodiscard]] ProjectRequests DrawProjectMenu(ProjectMenuState& state, std::span<const std::string> recent,
                                              std::span<const ProjectTemplate> templates);

} // namespace interi::editor
