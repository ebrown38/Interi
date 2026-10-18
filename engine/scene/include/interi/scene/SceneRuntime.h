// SPDX-License-Identifier: MIT
#pragma once

// A scene running: its objects in their own World, their chains in a ChainExecutor, one fixed step at a time.
// Play-in-editor runs one on a copy of the edited scene; games (M17) run the startup scene the same way.
//
// Each Step:
//   1. ChainExecutor::Step (a sync point; ECS changes through CommandBuffers).
//   2. World transforms chains changed (world.Move, world.Rotate) are adopted into the objects' local transforms,
//      relative to the parent's transform before the step, so a moved parent still carries its children.
//   3. Transforms propagate; prefabs chains spawned are expanded (their roots raise trigger.Spawned next step).

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Event.h>
#include <interi/chain/Executor.h>
#include <interi/core/Result.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Prefab.h>
#include <interi/scene/SceneFile.h>
#include <interi/values/Reflection.h>
#include <interi/values/ValueRegistry.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace interi::scene
{

/// The chains an object runs (bound to it: `self` is the object), or global chains (`global`: bound to no entity,
/// they react to every object's triggers).
struct ChainsComponent
{
    static constexpr const char* kComponentName = "interi.Chains";
    values::ValueList chains{.element = values::ValueType::AssetRef, .items = {}};
    bool global = false;
};

void RegisterChainsReflection(values::ReflectionRegistry& registry);

struct SceneRuntimeDesc
{
    chain::ExecutorDesc executor;
};

class SceneRuntime
{
public:
    /// Instantiates `document` (prefab instances expanded from `files`) and adds every ChainsComponent's chains. A
    /// chain that can't be read or doesn't compile is reported in Warnings() and skipped; a scene that can't be
    /// instantiated fails. `reflection`, `chains`, `values` (may be nullptr), `files` and `jobs` must outlive the
    /// runtime.
    [[nodiscard]] static Result<std::unique_ptr<SceneRuntime>>
    Create(SceneDocument document, const values::ReflectionRegistry& reflection, chain::ChainRegistry& chains,
           values::ValueRegistry* values, const IFileSystem& files, JobSystem& jobs, const SceneRuntimeDesc& desc = {});
    ~SceneRuntime();

    SceneRuntime(const SceneRuntime&) = delete;
    SceneRuntime& operator=(const SceneRuntime&) = delete;
    SceneRuntime(SceneRuntime&&) = delete;
    SceneRuntime& operator=(SceneRuntime&&) = delete;

    /// One fixed step (see the top of this header). Call it on the thread that owns the world.
    void Step();

    [[nodiscard]] World& GetWorld() noexcept { return m_world; }
    [[nodiscard]] chain::ChainExecutor& Executor() noexcept { return m_executor; }
    [[nodiscard]] std::uint64_t StepIndex() const noexcept { return m_executor.StepIndex(); }
    /// Problems so far: chains that couldn't be read or compiled, prefabs that couldn't be spawned.
    [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept { return m_warnings; }

private:
    SceneRuntime(const values::ReflectionRegistry& reflection, chain::ChainRegistry& chains,
                 values::ValueRegistry* values, const IFileSystem& files, JobSystem& jobs,
                 const SceneRuntimeDesc& desc);

    const values::ReflectionRegistry* m_reflection;
    JobSystem* m_jobs;
    World m_world;
    CommandBuffers m_commands;
    chain::ChainExecutor m_executor;
    PrefabLibrary m_prefabs;
    std::vector<std::string> m_warnings;
};

} // namespace interi::scene
