// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/render/Particles.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

/// One kind of particle a breaking bond throws off (dust, chips, sparks...).
struct ParticlePreset
{
    float perSquareMetre = 20.0f; ///< Particles per m^2 of bond area, clamped to [minCount, maxCount].
    std::uint32_t minCount = 1;
    std::uint32_t maxCount = 32;
    float minSpeed = 0.5f; ///< m/s, away from the crack.
    float maxSpeed = 2.0f;
    float spread = 0.8f; ///< Random direction added to the crack normal (0 = straight along it).
    float minLifetime = 0.5f;
    float maxLifetime = 1.5f;
    float minSize = 0.05f;
    float maxSize = 0.15f;
    std::uint32_t color = render::PackColor(160, 160, 160, 200);
    std::uint8_t colorJitter = 20; ///< Each particle's RGB is darkened by up to this.
    float drag = 0.5f;
    float gravityScale = 1.0f;
    bool additive = false;
};

/// The presets of each interior look (InteriorPattern, i.e. DestructionMaterial::interiorMaterialId).
using BreakEffectTable = std::array<std::vector<ParticlePreset>, kInteriorPatternCount>;

/// Concrete: lingering dust plus falling chips. Steel and sheet metal: sparks (additive). Wood: splinters and
/// light dust. Glass: glinting shards.
[[nodiscard]] BreakEffectTable DefaultBreakEffects();

/// Appends the particles of one break to `out`: every preset of the look, positioned at the crack and thrown off
/// to both sides of it. Uses only integer randomness seeded by `seed`, so it is reproducible.
void SpawnBreakParticles(const BreakEvent& event, std::span<const ParticlePreset> presets, std::uint64_t seed,
                         std::vector<render::Particle>& out);

/// Turns a DestructionWorld's break events into GPU particles each Update.
class BreakEffects
{
public:
    explicit BreakEffects(BreakEffectTable table = DefaultBreakEffects(), std::uint64_t seed = 1);

    /// Appends the particles for the last Update's BreakEvents to `out`. Events whose material has an interior
    /// look outside the table spawn nothing.
    void Spawn(const DestructionWorld& destruction, std::vector<render::Particle>& out);

private:
    BreakEffectTable m_table;
    std::uint64_t m_seed;
    std::uint64_t m_calls = 0;
};

} // namespace interi::destruction
