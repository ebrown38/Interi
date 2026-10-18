// SPDX-License-Identifier: MIT
#pragma once

// Physics-free trigger volumes: axis-aligned boxes and spheres that report which actors (points with a radius) are
// inside, so simple games need no Physics Engine. OverlapTracker keeps the pairs between calls and returns only the
// changes, in a deterministic order: by (volume id, actor id), ends before begins.

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <vector>

namespace interi
{

enum class VolumeShape : std::uint8_t
{
    Box,   ///< Axis-aligned, `halfExtents` around `center`.
    Sphere ///< `radius` around `center`.
};

struct TriggerVolume
{
    std::uint64_t id = 0;
    VolumeShape shape = VolumeShape::Box;
    Vec3 center;
    Vec3 halfExtents{0.5f};
    float radius = 0.5f;
    std::uint32_t mask = ~0u; ///< Detects actors whose layers share a bit with it.
};

struct TriggerActor
{
    std::uint64_t id = 0;
    Vec3 position;
    float radius = 0.0f; ///< 0: a point.
    std::uint32_t layers = 1u;
};

/// True when the actor touches the volume (touching counts).
[[nodiscard]] bool Overlaps(const TriggerVolume& volume, const TriggerActor& actor) noexcept;

struct OverlapEvent
{
    std::uint64_t volume = 0;
    std::uint64_t actor = 0;
    bool begin = true; ///< false: the actor left (or was removed).

    friend bool operator==(const OverlapEvent&, const OverlapEvent&) noexcept = default;
};

class OverlapTracker
{
public:
    /// Compares the current overlaps with the previous call's and returns the changes. A volume or actor that is
    /// no longer passed ends its pairs. Ids must be unique within each list (asserted).
    [[nodiscard]] std::vector<OverlapEvent> Update(const std::vector<TriggerVolume>& volumes,
                                                   const std::vector<TriggerActor>& actors);

    /// Pairs overlapping as of the last Update, sorted by (volume, actor).
    [[nodiscard]] std::size_t ActiveCount() const noexcept { return m_active.size(); }
    [[nodiscard]] bool IsOverlapping(std::uint64_t volume, std::uint64_t actor) const;
    void Clear() noexcept { m_active.clear(); }

private:
    struct Pair
    {
        std::uint64_t volume;
        std::uint64_t actor;
        friend auto operator<=>(const Pair&, const Pair&) noexcept = default;
    };
    std::vector<Pair> m_active;
};

} // namespace interi
