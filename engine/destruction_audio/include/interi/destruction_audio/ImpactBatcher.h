// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

/// One contact to be heard.
struct ImpactSound
{
    Vec3 point;
    float impulse = 0.0f; ///< N·s.
    std::uint32_t bank = 0;
};

/// Contacts of one bank in one region, heard as one voice.
struct ImpactGroup
{
    std::uint32_t bank = 0;
    Vec3 position;            ///< Impulse-weighted centroid of the contacts.
    float impulse = 0.0f;     ///< Sum over the contacts (loudness).
    float peakImpulse = 0.0f; ///< Largest single contact.
    std::uint32_t count = 0;
};

/// Groups a frame's contacts by (bank, grid cell of `regionSize`, cells centred on multiples of it), so a pile of
/// debris hitting the ground at once is one voice, not hundreds. Groups come out sorted by bank, then cell, so the
/// result does not depend on the order contacts were added in.
class ImpactBatcher
{
public:
    explicit ImpactBatcher(float regionSize = 4.0f);

    void Add(const ImpactSound& sound);
    /// The groups of everything added since the last Flush; the batcher is empty afterwards. Valid until the next
    /// Flush.
    [[nodiscard]] std::span<const ImpactGroup> Flush();

    [[nodiscard]] float RegionSize() const noexcept { return m_regionSize; }

private:
    struct Keyed
    {
        std::uint32_t bank = 0;
        std::int32_t cell[3] = {};
        ImpactSound sound;
    };

    float m_regionSize;
    std::vector<Keyed> m_sounds;
    std::vector<ImpactGroup> m_groups;
};

} // namespace interi::destruction
