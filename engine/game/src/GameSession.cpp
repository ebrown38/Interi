// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/game/GameSession.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneSave.h>

#include <format>

namespace interi::game
{

GameSession::~GameSession()
{
    // The engine's save section points at the scene runtime.
    if (m_modules != nullptr)
    {
        m_modules->Host().Saves().RemoveProvider(kEngineSaveProvider);
    }
}

Result<std::unique_ptr<GameSession>> GameSession::Open(const GameDesc& desc)
{
    std::unique_ptr<GameSession> session(new GameSession());
    INTERI_TRY_ASSIGN(project, sdk::LoadProject(desc.projectDirectory));
    session->m_project = std::move(project);
    session->m_scenePath = desc.scene.empty() ? session->m_project.startupScene : desc.scene;
    if (session->m_scenePath.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "the project has no startup scene");
    }

    JobSystemDesc jobs;
    jobs.workerCount = desc.workerCount;
    session->m_jobs = std::make_unique<JobSystem>(jobs);

    session->m_projectFiles = std::make_unique<NativeFileSystem>(session->m_project.directory);
    INTERI_TRY(session->m_files.Mount("", *session->m_projectFiles));
    scene::RegisterSceneRuntimeReflection(session->m_reflection);

    INTERI_TRY_ASSIGN(modules, ProjectModules::Open(session->m_project, session->m_world, desc.modules));
    session->m_modules = std::move(modules);

    if (!desc.saveDirectory.empty())
    {
        session->m_saveFiles = std::make_unique<NativeFileSystem>(desc.saveDirectory);
        session->m_slots = std::make_unique<SaveSlots>(*session->m_saveFiles, "saves", desc.saveSlots);
    }
    INTERI_TRY(session->StartScene());
    return session;
}

Result<void> GameSession::StartScene()
{
    INTERI_TRY_ASSIGN(document, scene::LoadScene(m_files, m_scenePath));
    // Replace the running scene only once the new one is known to load: parse first, then tear down.
    hotreload::ModuleHost& host = m_modules->Host();
    host.Saves().RemoveProvider(kEngineSaveProvider);
    m_scene.reset();
    // A restart leaves nothing of the previous run: every scene object goes (module-owned entities without a
    // stable id are not ours to remove).
    for (const auto& [id, node] : scene::BuildHierarchy(m_world).nodes)
    {
        m_world.DestroyEntity(node.entity);
    }
    scene::SceneRuntimeDesc runtimeDesc;
    runtimeDesc.world = &m_world;
    INTERI_TRY_ASSIGN(runtime, scene::SceneRuntime::Create(std::move(document), m_reflection, host.Chains(),
                                                           &host.Data().Values(), m_files, *m_jobs, runtimeDesc));
    m_scene = std::move(runtime);
    return scene::RegisterSceneSaveSection(host.Saves(), *m_scene);
}

void GameSession::Step()
{
    m_modules->Host().RunSystems(*m_jobs, m_scene->Commands());
    m_scene->Step();
}

void GameSession::RaiseInputAction(std::string_view action)
{
    m_scene->Executor().RaiseInputAction(action);
}

Result<void> GameSession::Restart()
{
    return StartScene();
}

Result<void> GameSession::SaveToSlot(std::uint32_t slot, std::string label)
{
    if (m_slots == nullptr)
    {
        return MakeError(ErrorCode::Unsupported, "this game has no save directory");
    }
    return m_slots->Write(slot, m_modules->Host().Saves().Capture(std::move(label)));
}

Result<RestoreReport> GameSession::LoadFromSlot(std::uint32_t slot)
{
    if (m_slots == nullptr)
    {
        return MakeError(ErrorCode::Unsupported, "this game has no save directory");
    }
    INTERI_TRY_ASSIGN(game, m_slots->Read(slot));
    INTERI_TRY(m_modules->Host().Saves().Validate(game));
    INTERI_TRY(StartScene());
    return m_modules->Host().Saves().Apply(game);
}

std::vector<SaveSlots::Info> GameSession::Slots() const
{
    return m_slots == nullptr ? std::vector<SaveSlots::Info>{} : m_slots->List();
}

std::vector<std::string> GameSession::Warnings() const
{
    std::vector<std::string> warnings = m_modules->Warnings();
    if (m_scene != nullptr)
    {
        warnings.insert(warnings.end(), m_scene->Warnings().begin(), m_scene->Warnings().end());
    }
    return warnings;
}

} // namespace interi::game
