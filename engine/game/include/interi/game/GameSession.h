// SPDX-License-Identifier: MIT
#pragma once

// A project running as a game: its modules (ProjectModules) loaded on a World, the startup scene in a SceneRuntime on
// that World, input actions raised into the chains, and save slots.
//
// Step() is one fixed step: the modules' systems run (a sync point on the scene's command buffers), then the scene
// (chains, transforms, triggers). The caller owns time (runtime/FixedStepClock.h) and input (InputMapper): raise the
// actions that were pressed with RaiseInputAction before the step.
//
// Saves are a SaveGame of sections (runtime/SaveGame.h): the scene's ("scene.state") plus the ones the modules
// registered. Loading restarts the scene from the startup scene and applies the sections to the fresh runtime.

#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/game/ProjectModules.h>
#include <interi/jobs/JobSystem.h>
#include <interi/nav/NavMesh.h>
#include <interi/platform/FileSystem.h>
#include <interi/runtime/SaveGame.h>
#include <interi/scene/SceneRuntime.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace interi::game
{

struct GameDesc
{
    std::filesystem::path projectDirectory;
    ProjectModulesDesc modules;
    /// Where save slots are written; empty: saves are unavailable.
    std::filesystem::path saveDirectory;
    std::uint32_t saveSlots = 3;
    std::uint32_t workerCount = JobSystemDesc::kAutoWorkerCount;
    /// A scene to run instead of the project's startup scene (project-relative).
    std::string scene;
    /// A cooked navigation mesh (`.inav`, project-relative) loaded into ModuleHost::Navigation(). A missing file is
    /// fine (no navigation); one that doesn't read is a warning. Empty: none.
    std::string navMesh = "content/navmesh.inav";
};

class GameSession
{
public:
    /// Loads project.json from `desc.projectDirectory`, its modules and data, and starts the scene. Fails (and
    /// loads nothing) when any of them doesn't load. Chains that don't compile are warnings (Warnings()).
    [[nodiscard]] static Result<std::unique_ptr<GameSession>> Open(const GameDesc& desc);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;

    /// One fixed step (see the top of this header).
    void Step();
    /// The input action was pressed: chains with a trigger.InputAction node for it react in the next Step.
    void RaiseInputAction(std::string_view action);

    /// Throws the running scene away and starts the startup scene again (run-time Values reset).
    Result<void> Restart();

    /// Saves the current state to `slot` under `label`. Unsupported without a saveDirectory.
    Result<void> SaveToSlot(std::uint32_t slot, std::string label);
    /// Reads `slot`, restarts the scene and applies the save. Nothing changes when the slot doesn't read or a
    /// section is newer than this build knows.
    Result<RestoreReport> LoadFromSlot(std::uint32_t slot);
    /// The slots for a load menu (empty without a saveDirectory).
    [[nodiscard]] std::vector<SaveSlots::Info> Slots() const;

    [[nodiscard]] scene::SceneRuntime& Scene() noexcept { return *m_scene; }
    [[nodiscard]] hotreload::ModuleHost& Modules() noexcept { return m_modules->Host(); }
    [[nodiscard]] values::DataRegistry& Data() noexcept { return m_modules->Host().Data(); }
    [[nodiscard]] chain::RuntimeValues& Values() noexcept { return m_scene->Executor().Values(); }
    [[nodiscard]] IFileSystem& Files() noexcept { return m_files; }
    [[nodiscard]] const sdk::Project& Project() const noexcept { return m_project; }
    [[nodiscard]] World& GetWorld() noexcept { return m_world; }
    /// Everything that went wrong without stopping the game: project data migrations, chains that didn't compile.
    [[nodiscard]] std::vector<std::string> Warnings() const;

private:
    GameSession() = default;
    Result<void> StartScene();
    /// Walks the navigation agents (nav::StepNavAgents) and raises their nav.Arrived / nav.Failed triggers.
    void StepNavigation();

    sdk::Project m_project;
    std::string m_scenePath;
    std::unique_ptr<JobSystem> m_jobs;
    World m_world;
    std::unique_ptr<NativeFileSystem> m_projectFiles;
    VirtualFileSystem m_files;
    std::unique_ptr<ProjectModules> m_modules;
    values::ReflectionRegistry m_reflection;
    std::unique_ptr<scene::SceneRuntime> m_scene;
    std::unique_ptr<NativeFileSystem> m_saveFiles;
    std::vector<std::string> m_warnings;
    std::unique_ptr<SaveSlots> m_slots;
};

} // namespace interi::game
