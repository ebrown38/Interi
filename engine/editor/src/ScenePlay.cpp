// SPDX-License-Identifier: MIT
#include <interi/editor/DataEditing.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Prefabs.h>
#include <interi/editor/ScenePlay.h>

namespace interi::editor
{

Result<std::unique_ptr<ScenePlaySession>> ScenePlaySession::Start(const EditorScene& scene,
                                                                  chain::ChainRegistry& chains,
                                                                  values::ValueRegistry* values,
                                                                  const IFileSystem& files, JobSystem& jobs, bool trace)
{
    scene::SceneDocument document = scene.ToDocument();
    scene::CollapsePrefabs(document, PrefabLoaderFor(files), scene.Reflection()); // edits become overrides
    scene::SceneRuntimeDesc desc;
    desc.executor.trace = trace;
    INTERI_TRY_ASSIGN(runtime, scene::SceneRuntime::Create(std::move(document), scene.Reflection(), chains, values,
                                                           files, jobs, desc));
    return std::unique_ptr<ScenePlaySession>(new ScenePlaySession(std::move(runtime)));
}

void ScenePlaySession::Advance(std::chrono::nanoseconds elapsed)
{
    if (m_paused)
    {
        return;
    }
    for (std::uint32_t steps = m_clock.Advance(elapsed); steps > 0 && !m_paused; --steps)
    {
        m_runtime->Step();
        AfterStep();
    }
}

void ScenePlaySession::StepOnce()
{
    m_runtime->Step();
    AfterStep();
}

void ScenePlaySession::AfterStep()
{
    if (m_observer && m_observer(*this))
    {
        m_paused = true;
    }
}

std::size_t EndPlay(const ScenePlaySession& session, bool keep, values::DataRegistry* data, UndoStack& undo,
                    std::size_t undoMark)
{
    if (!keep)
    {
        while (undo.Applied() > undoMark && undo.Undo())
        {
        }
        return 0;
    }
    return data == nullptr ? 0 : KeepRuntimeValues(session.Runtime().Executor().Values(), *data, undo);
}

std::size_t KeepRuntimeValues(const chain::RuntimeValues& runtime, values::DataRegistry& data, UndoStack& undo)
{
    std::size_t kept = 0;
    for (const auto& [id, value] : runtime.Overrides())
    {
        if (EditValue(data, undo, id, value))
        {
            ++kept;
        }
    }
    return kept;
}

} // namespace interi::editor
