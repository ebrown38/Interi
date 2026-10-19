// SPDX-License-Identifier: MIT
#pragma once

// Play-in-editor: the edited scene runs with its chains (scene::SceneRuntime) in a world of its own, so stopping is
// just dropping the session and the document is never touched. Physics-free: a module that simulates physics plays
// the scene itself.

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/editor/UndoStack.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/FileSystem.h>
#include <interi/runtime/FixedStepClock.h>
#include <interi/scene/SceneRuntime.h>
#include <interi/values/DataRegistry.h>

#include <chrono>
#include <functional>
#include <memory>

namespace interi::editor
{

class EditorScene;

class ScenePlaySession
{
public:
    /// Copies the scene (expanded prefab instances collapsed to their current overrides, then expanded again by
    /// the runtime) and starts it; `trace` records the chain executor's trace for the debugger. `chains` (with at least
    /// the core library), `values` (may be nullptr), `files` and `jobs` must outlive the session.
    [[nodiscard]] static Result<std::unique_ptr<ScenePlaySession>>
    Start(const EditorScene& scene, chain::ChainRegistry& chains, values::ValueRegistry* values,
          const IFileSystem& files, JobSystem& jobs, bool trace = false);

    /// Runs the fixed steps `elapsed` adds up to (none while paused).
    void Advance(std::chrono::nanoseconds elapsed);
    /// One step now (paused or not).
    void StepOnce();
    /// Called after every step (the chain debugger reads the trace here). Returning true pauses the session: a
    /// breakpoint was hit, and Advance runs no further steps.
    using StepObserver = std::function<bool(const ScenePlaySession&)>;
    void SetStepObserver(StepObserver observer) { m_observer = std::move(observer); }
    void SetPaused(bool paused) noexcept { m_paused = paused; }
    [[nodiscard]] bool Paused() const noexcept { return m_paused; }

    [[nodiscard]] scene::SceneRuntime& Runtime() noexcept { return *m_runtime; }
    [[nodiscard]] const scene::SceneRuntime& Runtime() const noexcept { return *m_runtime; }
    [[nodiscard]] std::uint64_t StepIndex() const noexcept { return m_runtime->StepIndex(); }

private:
    explicit ScenePlaySession(std::unique_ptr<scene::SceneRuntime> runtime) noexcept
        : m_runtime(std::move(runtime))
    {
    }

    void AfterStep();

    std::unique_ptr<scene::SceneRuntime> m_runtime;
    StepObserver m_observer;
    FixedStepClock m_clock;
    bool m_paused = false;
};

/// Ends a play session. Keeping, the Values chains set become project edits (KeepRuntimeValues; `data` may be
/// nullptr) and every edit made during play stays. Not keeping, the edits made during play (Values panel, scene)
/// are undone back to `undoMark` (UndoStack::Applied() when play started) and chain Values are dropped. Returns how
/// many chain Values were kept.
std::size_t EndPlay(const ScenePlaySession& session, bool keep, values::DataRegistry* data, UndoStack& undo,
                    std::size_t undoMark);

/// Keeps the Values chains set during play: each run-time override becomes a project Value edit (one undo step
/// each, through DataEditing). Returns how many were kept; overrides that no longer fit are skipped.
std::size_t KeepRuntimeValues(const chain::RuntimeValues& runtime, values::DataRegistry& data, UndoStack& undo);

} // namespace interi::editor
