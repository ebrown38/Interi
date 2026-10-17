// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/destruction/StressSolver.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace interi::destruction
{

struct StressAudioSettings
{
    float creakThreshold = 0.75f;        ///< Utilization that sets off a creak.
    float rearmThreshold = 0.6f;         ///< A region creaks again only after its peak fell below this...
    float cooldown = 2.0f;               ///< ...and this many seconds passed.
    float regionSize = 4.0f;             ///< m: creaks are per grid cell of this size (asset space).
    std::uint32_t bondsPerUpdate = 8192; ///< Bonds checked per Update, shared by all instances (round-robin).
};

/// A structure under strain somewhere.
struct CreakEvent
{
    DestructibleId instance;
    Vec3 position;            ///< World space: the strained bond.
    float utilization = 0.0f; ///< Of that bond.
};

/// Creaking and groaning as stress approaches the limits. Each Update checks a budget of bonds (cycling through
/// every instance's bonds over several frames) and reports a creak where a bond's utilization crosses
/// creakThreshold, at most once per region until the region relaxes below rearmThreshold and the cooldown passed.
class StressAudio
{
public:
    explicit StressAudio(const StressAudioSettings& settings = {});

    /// Appends this Update's creaks to `out` (instance order, then bond order).
    void Update(const DestructionWorld& destruction, float deltaSeconds, std::vector<CreakEvent>& out);

    [[nodiscard]] const StressAudioSettings& Settings() const noexcept { return m_settings; }

private:
    using Cell = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

    struct Region
    {
        float peak = 0.0f; ///< This cycle's largest utilization.
        bool armed = true;
        float sinceCreak = 1.0e9f;
    };

    struct Tracked
    {
        DestructibleId id;
        std::uint32_t cursor = 0; ///< Next bond to check.
        std::map<Cell, Region> regions;
    };

    [[nodiscard]] Cell CellOf(const Vec3& point) const;

    StressAudioSettings m_settings;
    std::vector<Tracked> m_tracked; ///< By instance index order.
    std::uint32_t m_next = 0;       ///< Which tracked instance the budget starts with.
};

struct RumbleSettings
{
    float regionSize = 8.0f; ///< m: pieces starting to fall in one cell are one rumble.
};

/// Mass that started falling this Update in one region.
struct CollapseEvent
{
    Vec3 position; ///< Mass-weighted centroid (world space).
    float mass = 0.0f;
};

/// Collapse rumble: finds the falling (unanchored) pieces each Update creates and sums their mass per region.
class CollapseAudio
{
public:
    explicit CollapseAudio(const RumbleSettings& settings = {});

    /// Call after DestructionWorld::Update. Appends the regions' events to `out`, sorted by cell.
    void Update(const World& world, const DestructionWorld& destruction, std::vector<CollapseEvent>& out);

private:
    RumbleSettings m_settings;
    std::vector<std::pair<DestructibleId, std::uint32_t>> m_seen; ///< Pieces already counted, per instance.
};

} // namespace interi::destruction
