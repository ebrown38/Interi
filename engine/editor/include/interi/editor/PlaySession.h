// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/Damage.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>
#include <interi/editor/Replay.h>
#include <interi/gameplay/Level.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace interi
{
class IFileSystem;
class JobSystem;
} // namespace interi

namespace interi::editor
{

/// Play-in-editor: the level spawned into fresh ECS, physics and destruction worlds (static boxes and
/// destructibles; spawn points are not played) and stepped at a fixed rate. Explosions set off with Detonate are
/// recorded with their step, so the session can be saved as a Replay and reproduced bit for bit. Needs a live
/// physics::JoltRuntime.
class PlaySession
{
public:
    static constexpr float kStep = 1.0f / 60.0f;

    [[nodiscard]] static Result<std::unique_ptr<PlaySession>> Start(const gameplay::LevelDef& level,
                                                                    const IFileSystem& files, JobSystem& jobs);
    ~PlaySession();

    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    PlaySession(PlaySession&&) = delete;
    PlaySession& operator=(PlaySession&&) = delete;

    /// An explosion at the start of the next step (recorded).
    void Detonate(const destruction::RadialDamage& damage);
    /// One fixed step: queued explosions, physics, then destruction.
    void Step();

    [[nodiscard]] std::uint64_t StepIndex() const noexcept { return m_step; }
    /// XXH3 of the full physics state (Jolt SaveState) and every instance's bond health, in instance order.
    [[nodiscard]] std::uint64_t StateHash() const;
    /// Everything since Start, ending now (steps so far and the current hash). `levelPath` is informational.
    [[nodiscard]] Replay Recording(std::string levelPath) const;

    [[nodiscard]] World& GetWorld() noexcept { return m_world; }
    [[nodiscard]] destruction::DestructionWorld& Destruction() noexcept { return m_destruction; }
    [[nodiscard]] const physics::PhysicsWorld& Physics() const noexcept { return m_physics; }
    [[nodiscard]] const gameplay::LoadedLevel& Level() const noexcept { return m_level; }

private:
    PlaySession(const gameplay::LevelDef& level, JobSystem& jobs);

    JobSystem& m_jobs;
    std::string m_levelJson;
    World m_world;
    physics::PhysicsWorld m_physics;
    physics::PhysicsSync m_sync;
    destruction::DestructionWorld m_destruction;
    gameplay::LoadedLevel m_level;
    std::vector<ReplayEvent> m_events;
    std::vector<destruction::RadialDamage> m_queued;
    std::uint64_t m_step = 0;
};

/// Spawns a replay's level fresh, applies its events at their steps and runs its steps; returns the final state
/// hash (compare it with Replay::finalHash). Fails if the level or an asset can't be loaded.
[[nodiscard]] Result<std::uint64_t> RunReplay(const Replay& replay, const IFileSystem& files, JobSystem& jobs);

} // namespace interi::editor
