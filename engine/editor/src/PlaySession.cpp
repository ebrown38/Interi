// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/editor/PlaySession.h>
#include <interi/platform/FileSystem.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/StateRecorderImpl.h>
// clang-format on

#include <string>

namespace interi::editor
{

PlaySession::PlaySession(const gameplay::LevelDef& level, JobSystem& jobs)
    : m_jobs(jobs)
    , m_levelJson(gameplay::WriteLevel(level))
    , m_physics(jobs)
    , m_sync(m_physics)
    , m_destruction(m_physics)
{
}

Result<std::unique_ptr<PlaySession>> PlaySession::Start(const gameplay::LevelDef& level, const IFileSystem& files,
                                                        JobSystem& jobs)
{
    std::unique_ptr<PlaySession> session(new PlaySession(level, jobs));
    INTERI_TRY_ASSIGN(loaded,
                      gameplay::SpawnLevel(level, files, session->m_world, jobs, session->m_destruction, nullptr));
    session->m_level = std::move(loaded);
    return session;
}

PlaySession::~PlaySession()
{
    gameplay::DespawnLevel(m_level, m_world, m_destruction);
}

void PlaySession::Detonate(const destruction::RadialDamage& damage)
{
    m_queued.push_back(damage);
    m_events.push_back({.step = m_step, .damage = damage});
}

void PlaySession::Step()
{
    for (const destruction::RadialDamage& damage : m_queued)
    {
        m_destruction.QueueDamage(damage, destruction::DamageKind::Explosion);
    }
    m_queued.clear();
    m_sync.Step(m_world, m_jobs, kStep);
    m_destruction.Update(m_world, m_jobs);
    ++m_step;
}

std::uint64_t PlaySession::StateHash() const
{
    JPH::StateRecorderImpl recorder;
    m_physics.System().SaveState(recorder);
    const std::string state = recorder.GetData();
    std::uint64_t hash = HashBytes(state.data(), state.size());
    for (const destruction::DestructibleId id : m_destruction.Instances())
    {
        const std::span<const float> health = m_destruction.BondHealth(id);
        hash = HashCombine(hash, HashBytes(health.data(), health.size_bytes()));
    }
    return hash;
}

Replay PlaySession::Recording(std::string levelPath) const
{
    return Replay{.levelPath = std::move(levelPath),
                  .levelJson = m_levelJson,
                  .steps = m_step,
                  .events = m_events,
                  .finalHash = StateHash()};
}

Result<std::uint64_t> RunReplay(const Replay& replay, const IFileSystem& files, JobSystem& jobs)
{
    INTERI_TRY_ASSIGN(level, gameplay::ParseLevel(replay.levelJson));
    INTERI_TRY_ASSIGN(session, PlaySession::Start(level, files, jobs));
    std::size_t next = 0;
    for (std::uint64_t step = 0; step < replay.steps; ++step)
    {
        while (next < replay.events.size() && replay.events[next].step == step)
        {
            session->Detonate(replay.events[next++].damage);
        }
        session->Step();
    }
    return session->StateHash();
}

} // namespace interi::editor
