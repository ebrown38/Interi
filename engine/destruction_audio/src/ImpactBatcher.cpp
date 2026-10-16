// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction_audio/ImpactBatcher.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace interi::destruction
{

ImpactBatcher::ImpactBatcher(float regionSize)
    : m_regionSize(regionSize)
{
    INTERI_ASSERT(regionSize > 0.0f);
}

void ImpactBatcher::Add(const ImpactSound& sound)
{
    Keyed keyed{.bank = sound.bank, .sound = sound};
    const float components[3] = {sound.point.x, sound.point.y, sound.point.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        // Cells are centred on multiples of the size, so a floor at y = 0 is mid-cell, not on a boundary where
        // contacts a hair above and below would split into two voices.
        keyed.cell[axis] = static_cast<std::int32_t>(std::floor(components[axis] / m_regionSize + 0.5f));
    }
    m_sounds.push_back(keyed);
}

std::span<const ImpactGroup> ImpactBatcher::Flush()
{
    const auto key = [](const Keyed& k) { return std::tie(k.bank, k.cell[0], k.cell[1], k.cell[2]); };
    // Within a group the order of contacts changes the float sums, so sort by content too.
    std::ranges::sort(
        m_sounds,
        [&](const Keyed& a, const Keyed& b)
        {
            return std::tuple_cat(key(a),
                                  std::tie(a.sound.point.x, a.sound.point.y, a.sound.point.z, a.sound.impulse)) <
                   std::tuple_cat(key(b), std::tie(b.sound.point.x, b.sound.point.y, b.sound.point.z, b.sound.impulse));
        });
    m_groups.clear();
    for (std::size_t i = 0; i < m_sounds.size();)
    {
        std::size_t end = i;
        ImpactGroup group;
        group.bank = m_sounds[i].bank;
        Vec3 weighted;
        while (end < m_sounds.size() && key(m_sounds[end]) == key(m_sounds[i]))
        {
            const ImpactSound& sound = m_sounds[end].sound;
            weighted += sound.point * sound.impulse;
            group.impulse += sound.impulse;
            group.peakImpulse = std::max(group.peakImpulse, sound.impulse);
            ++group.count;
            ++end;
        }
        group.position = group.impulse > 0.0f ? weighted / group.impulse : m_sounds[i].sound.point;
        m_groups.push_back(group);
        i = end;
    }
    m_sounds.clear();
    return m_groups;
}

} // namespace interi::destruction
