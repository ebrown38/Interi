// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/runtime/TriggerVolumes.h>

#include <algorithm>

namespace interi
{

namespace
{

float Clamp(float value, float low, float high) noexcept
{
    return std::min(std::max(value, low), high);
}

} // namespace

bool Overlaps(const TriggerVolume& volume, const TriggerActor& actor) noexcept
{
    if ((volume.mask & actor.layers) == 0u)
    {
        return false;
    }
    if (volume.shape == VolumeShape::Sphere)
    {
        const float reach = volume.radius + actor.radius;
        return LengthSquared(actor.position - volume.center) <= reach * reach;
    }
    // Distance from the actor's centre to the box, against its radius.
    const Vec3 offset = actor.position - volume.center;
    const Vec3 closest(Clamp(offset.x, -volume.halfExtents.x, volume.halfExtents.x),
                       Clamp(offset.y, -volume.halfExtents.y, volume.halfExtents.y),
                       Clamp(offset.z, -volume.halfExtents.z, volume.halfExtents.z));
    return LengthSquared(offset - closest) <= actor.radius * actor.radius;
}

std::vector<OverlapEvent> OverlapTracker::Update(const std::vector<TriggerVolume>& volumes,
                                                 const std::vector<TriggerActor>& actors)
{
    std::vector<Pair> now;
    // Volumes and actors are compared in id order, so `now` comes out sorted.
    std::vector<const TriggerVolume*> sortedVolumes;
    sortedVolumes.reserve(volumes.size());
    for (const TriggerVolume& volume : volumes)
    {
        sortedVolumes.push_back(&volume);
    }
    std::ranges::sort(sortedVolumes, {}, &TriggerVolume::id);
    std::vector<const TriggerActor*> sortedActors;
    sortedActors.reserve(actors.size());
    for (const TriggerActor& actor : actors)
    {
        sortedActors.push_back(&actor);
    }
    std::ranges::sort(sortedActors, {}, &TriggerActor::id);
    INTERI_ASSERT(std::ranges::adjacent_find(sortedVolumes, {}, &TriggerVolume::id) == sortedVolumes.end());
    INTERI_ASSERT(std::ranges::adjacent_find(sortedActors, {}, &TriggerActor::id) == sortedActors.end());

    for (const TriggerVolume* volume : sortedVolumes)
    {
        for (const TriggerActor* actor : sortedActors)
        {
            if (Overlaps(*volume, *actor))
            {
                now.push_back({volume->id, actor->id});
            }
        }
    }

    // Merge-walk the sorted old and new pairs.
    std::vector<OverlapEvent> events;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < m_active.size() || b < now.size())
    {
        if (b == now.size() || (a < m_active.size() && m_active[a] < now[b]))
        {
            events.push_back({.volume = m_active[a].volume, .actor = m_active[a].actor, .begin = false});
            ++a;
        }
        else if (a == m_active.size() || now[b] < m_active[a])
        {
            events.push_back({.volume = now[b].volume, .actor = now[b].actor, .begin = true});
            ++b;
        }
        else
        {
            ++a;
            ++b;
        }
    }
    m_active = std::move(now);
    return events;
}

bool OverlapTracker::IsOverlapping(std::uint64_t volume, std::uint64_t actor) const
{
    return std::ranges::binary_search(m_active, Pair{volume, actor});
}

} // namespace interi
