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
inline constexpr const char* kProjectSettingsWindow = "Project Settings";

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
    /// Why the last module the user tried to add was refused (shown in the menu until the next attempt).
    std::string refusal;
};

struct ProjectRequests
{
    std::optional<std::string> open; ///< A project folder.
    std::optional<std::string> createFolder;
    std::string createName;
    int createTemplate = -1;
    std::optional<std::string> addModule; ///< An installed module id to add to the open project.
};

/// The main menu bar's Project menu (it adds itself to the bar) with New (name, folder, template), Open (folder),
/// the recent projects, "Add module" (the installed modules `addable`) and the reason the last add was refused.
[[nodiscard]] ProjectRequests DrawProjectMenu(ProjectMenuState& state, std::span<const std::string> recent,
                                              std::span<const ProjectTemplate> templates,
                                              std::span<const std::string> addable = {});

struct ProjectSettingsRequests
{
    std::optional<std::string> pin; ///< Pin this Physics Engine.
    bool clearPin = false;          ///< Go back to choosing from the modules' defaults.
};

/// The Project Settings window: the selected Physics Engine, its version and why it was chosen, and a pin dropdown
/// listing only the engines every module supports.
[[nodiscard]] ProjectSettingsRequests DrawProjectSettingsPanel(const PhysicsEngineStatus& status);

} // namespace interi::editor
