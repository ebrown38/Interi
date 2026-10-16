// SPDX-License-Identifier: MIT
#pragma once

#include <interi/audio/Spatializer.h>
#include <interi/core/Result.h>
#include <interi/core/StringId.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::destruction
{

/// A family of sounds: one clip is picked per play. Clip names are looked up in the game's ClipLibrary.
struct AudioCue
{
    std::vector<std::string> clips;
    float volume = 1.0f;
    float priority = 1.0f; ///< Voice-limiter importance (times audible gain).
    audio::AttenuationCurve attenuation;
};

/// Impact sounds for a pair of surface materials, in either order. "*" matches any material.
struct ImpactBank : AudioCue
{
    std::string materialA = "*";
    std::string materialB = "*";
    StringId idA;               ///< StringId(materialA); invalid for "*".
    StringId idB;               ///< StringId(materialB); invalid for "*".
    float minImpulse = 200.0f;  ///< N·s: quieter contacts make no sound.
    float fullImpulse = 2.0e4f; ///< N·s: full volume from here on.
    float minVolume = 0.15f;    ///< Fraction of `volume` at minImpulse.
};

/// Collapse rumble, scaled by the mass that starts falling.
struct RumbleCue : AudioCue
{
    float minMass = 500.0f;  ///< kg: lighter pieces make no rumble.
    float fullMass = 1.0e5f; ///< kg: full volume from here on.
};

/// Every destruction sound, loaded from data (data/audio/destruction_audio.json):
///
///     { "impacts":   [ { "materials": ["concrete", "*"], "clips": ["impact_concrete"], "minImpulse": 200, ... } ],
///       "creak":     { "clips": ["creak"], "volume": 0.7, "priority": 0.8, "minDistance": 3, "maxDistance": 60 },
///       "rumble":    { "clips": ["rumble"], "minMass": 500, "fullMass": 100000, ... },
///       "explosion": { "clips": ["explosion"], ... } }
struct DestructionAudioConfig
{
    std::vector<ImpactBank> impacts;
    AudioCue creak;
    RumbleCue rumble;
    AudioCue explosion;
};

struct DestructionAudioConfigSet
{
    DestructionAudioConfig config;
    std::vector<std::string> warnings;
};

/// Parses the file above. Every cue needs a non-empty `clips` array of strings; `materials` is two strings. A
/// missing number takes its default and adds a warning, as does an unknown field. Malformed JSON, a wrong type or
/// an invalid value (volume, priority, impulses or masses < 0, fullImpulse <= minImpulse, fullMass <= minMass,
/// minVolume outside [0, 1], minDistance <= 0, maxDistance <= minDistance) fails with InvalidArgument.
[[nodiscard]] Result<DestructionAudioConfigSet> ParseDestructionAudioConfig(std::string_view json);
[[nodiscard]] Result<DestructionAudioConfigSet> LoadDestructionAudioConfig(const std::filesystem::path& path);

/// The bank for a contact between materials `a` and `b` (either order): an exact pair beats a pair with one "*",
/// which beats "*" / "*"; among equals the first in file order. An invalid id is a material without a name, which
/// only "*" matches. Nothing if no bank matches.
[[nodiscard]] std::optional<std::uint32_t> SelectImpactBank(std::span<const ImpactBank> banks, StringId a, StringId b);

/// Volume of an impact: 0 below minImpulse, then rising with log(impulse) from minVolume to 1 (times `volume`) at
/// fullImpulse.
[[nodiscard]] float ImpactVolume(const ImpactBank& bank, float impulse) noexcept;

/// Volume of a rumble for `mass` kg falling: 0 below minMass, rising with log(mass) to `volume` at fullMass.
[[nodiscard]] float RumbleVolume(const RumbleCue& cue, float mass) noexcept;

} // namespace interi::destruction
