// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/destruction/Damage.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace interi::editor
{

/// An explosion set off before step `step` of a play session.
struct ReplayEvent
{
    std::uint64_t step = 0;
    destruction::RadialDamage damage;
};

/// A recorded destruction event sequence: the level as it was played (its JSON, so later edits to the file don't
/// change the replay; assets are still read by path), the explosions by step, how many steps ran and the state
/// hash at the end.
struct Replay
{
    std::string levelPath;
    std::string levelJson;
    std::uint64_t steps = 0;
    std::vector<ReplayEvent> events;
    std::uint64_t finalHash = 0;
};

inline constexpr FourCC kReplayMagic = MakeFourCC("IRPL");
inline constexpr std::uint32_t kReplayVersion = 1;

/// File format: IRPL, version, the XXH3 of the payload, the payload (its own archive IRPP).
[[nodiscard]] std::vector<std::byte> WriteReplay(const Replay& replay);
/// Corrupt for a damaged file, Unsupported for another version.
[[nodiscard]] Result<Replay> ReadReplay(std::span<const std::byte> data);

} // namespace interi::editor
