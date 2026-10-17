// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction_audio/StructureAudio.h>

#include <algorithm>
#include <cmath>

namespace interi::destruction
{

StressAudio::StressAudio(const StressAudioSettings& settings)
    : m_settings(settings)
{
    INTERI_ASSERT(settings.regionSize > 0.0f);
}

StressAudio::Cell StressAudio::CellOf(const Vec3& point) const
{
    const auto cell = [&](float v) { return static_cast<std::int32_t>(std::floor(v / m_settings.regionSize)); };
    return {cell(point.x), cell(point.y), cell(point.z)};
}

void StressAudio::Update(const DestructionWorld& destruction, float deltaSeconds, std::vector<CreakEvent>& out)
{
    // Follow the live instances (in index order), keeping the state of those still alive.
    const std::vector<DestructibleId> instances = destruction.Instances();
    std::vector<Tracked> tracked;
    tracked.reserve(instances.size());
    for (const DestructibleId id : instances)
    {
        const auto it = std::ranges::find(m_tracked, id, &Tracked::id);
        if (it != m_tracked.end())
        {
            tracked.push_back(std::move(*it));
        }
        else
        {
            tracked.emplace_back().id = id;
        }
    }
    m_tracked = std::move(tracked);
    for (Tracked& t : m_tracked)
    {
        for (auto& [cell, region] : t.regions)
        {
            region.sinceCreak += deltaSeconds;
        }
    }
    if (m_tracked.empty())
    {
        return;
    }

    std::uint32_t budget = m_settings.bondsPerUpdate;
    const auto count = static_cast<std::uint32_t>(m_tracked.size());
    for (std::uint32_t n = 0; n < count && budget > 0; ++n)
    {
        Tracked& t = m_tracked[(m_next + n) % count];
        const StressSolver& solver = destruction.Stress(t.id);
        if (!solver.IsConverged())
        {
            continue; // forces of an unfinished solve are not loads yet
        }
        const auto health = destruction.BondHealth(t.id);
        const auto full = destruction.BondFullHealth(t.id);
        const Transform& pose = destruction.InstanceTransform(t.id);
        const auto bonds = static_cast<std::uint32_t>(solver.BondCount());
        for (; budget > 0 && bonds > 0; --budget)
        {
            const std::uint32_t bond = t.cursor;
            const float fraction = full[bond] > 0.0f ? std::max(health[bond], 0.0f) / full[bond] : 1.0f;
            const float utilization = BondUtilization(solver, bond, fraction);
            const Vec3 centroid = solver.Graph().bonds[bond].centroid;
            Region& region = t.regions[CellOf(centroid)];
            region.peak = std::max(region.peak, utilization);
            if (utilization >= m_settings.creakThreshold && region.armed && region.sinceCreak >= m_settings.cooldown)
            {
                out.push_back(
                    {.instance = t.id, .position = pose.TransformPoint(centroid), .utilization = utilization});
                region.armed = false;
                region.sinceCreak = 0.0f;
            }
            if (++t.cursor == bonds)
            {
                // A full cycle: regions that relaxed may creak again.
                t.cursor = 0;
                for (auto& [cell, r] : t.regions)
                {
                    r.armed = r.armed || r.peak < m_settings.rearmThreshold;
                    r.peak = 0.0f;
                }
                if (bonds <= m_settings.bondsPerUpdate)
                {
                    --budget;
                    break; // small structures: one cycle per Update is enough
                }
            }
        }
    }
    m_next = (m_next + 1) % count;
}

CollapseAudio::CollapseAudio(const RumbleSettings& settings)
    : m_settings(settings)
{
    INTERI_ASSERT(settings.regionSize > 0.0f);
}

void CollapseAudio::Update(const World& world, const DestructionWorld& destruction, std::vector<CollapseEvent>& out)
{
    struct Falling
    {
        std::tuple<std::int32_t, std::int32_t, std::int32_t> cell;
        Vec3 weighted;
        float mass = 0.0f;
    };
    std::vector<Falling> falling;
    std::vector<std::pair<DestructibleId, std::uint32_t>> seen;
    for (const DestructibleId id : destruction.Instances())
    {
        const auto it = std::ranges::find(m_seen, id, &std::pair<DestructibleId, std::uint32_t>::first);
        // A new instance's initial pieces are not a collapse.
        const std::uint32_t first = it != m_seen.end() ? it->second : destruction.PieceCount(id);
        const std::uint32_t count = destruction.PieceCount(id);
        seen.emplace_back(id, count);
        const DestructibleAsset& asset = destruction.Asset(id);
        for (std::uint32_t piece = first; piece < count; ++piece)
        {
            const Entity entity = destruction.PieceEntity(id, piece);
            const Transform* pose = world.IsAlive(entity) ? world.Get<Transform>(entity) : nullptr;
            if (destruction.PieceAnchored(id, piece) || pose == nullptr)
            {
                continue;
            }
            Vec3 weighted;
            float mass = 0.0f;
            for (const std::uint32_t chunk : destruction.PieceChunks(id, piece))
            {
                const float m = asset.chunks[chunk].mass;
                weighted += pose->TransformPoint(asset.chunks[chunk].centroid) * m;
                mass += m;
            }
            if (mass > 0.0f)
            {
                const Vec3 centre = weighted / mass;
                const auto cell = [&](float v)
                { return static_cast<std::int32_t>(std::floor(v / m_settings.regionSize)); };
                falling.push_back(
                    {.cell = {cell(centre.x), cell(centre.y), cell(centre.z)}, .weighted = weighted, .mass = mass});
            }
        }
    }
    m_seen = std::move(seen);
    std::ranges::stable_sort(falling, {}, &Falling::cell);
    for (std::size_t i = 0; i < falling.size();)
    {
        Vec3 weighted;
        float mass = 0.0f;
        std::size_t end = i;
        for (; end < falling.size() && falling[end].cell == falling[i].cell; ++end)
        {
            weighted += falling[end].weighted;
            mass += falling[end].mass;
        }
        out.push_back({.position = weighted / mass, .mass = mass});
        i = end;
    }
}

} // namespace interi::destruction
