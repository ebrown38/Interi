// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/core/Random.h>
#include <interi/destruction_render/BreakEffects.h>

#include <algorithm>
#include <cmath>

namespace interi::destruction
{

BreakEffectTable DefaultBreakEffects()
{
    using render::PackColor;
    const ParticlePreset concreteDust{.perSquareMetre = 24.0f,
                                      .minCount = 4,
                                      .maxCount = 48,
                                      .minSpeed = 0.3f,
                                      .maxSpeed = 1.2f,
                                      .spread = 1.2f,
                                      .minLifetime = 1.5f,
                                      .maxLifetime = 3.0f,
                                      .minSize = 0.3f,
                                      .maxSize = 0.8f,
                                      .color = PackColor(150, 145, 135, 140),
                                      .colorJitter = 25,
                                      .drag = 1.5f,
                                      .gravityScale = -0.03f};
    const ParticlePreset concreteChips{.perSquareMetre = 16.0f,
                                       .minCount = 2,
                                       .maxCount = 32,
                                       .minSpeed = 1.5f,
                                       .maxSpeed = 4.0f,
                                       .spread = 0.9f,
                                       .minLifetime = 0.8f,
                                       .maxLifetime = 1.4f,
                                       .minSize = 0.03f,
                                       .maxSize = 0.07f,
                                       .color = PackColor(105, 102, 98, 255),
                                       .colorJitter = 30,
                                       .drag = 0.2f,
                                       .gravityScale = 1.0f};
    const ParticlePreset sparks{.perSquareMetre = 40.0f,
                                .minCount = 6,
                                .maxCount = 64,
                                .minSpeed = 3.0f,
                                .maxSpeed = 8.0f,
                                .spread = 0.7f,
                                .minLifetime = 0.25f,
                                .maxLifetime = 0.7f,
                                .minSize = 0.02f,
                                .maxSize = 0.04f,
                                .color = PackColor(255, 170, 70, 255),
                                .colorJitter = 40,
                                .drag = 0.4f,
                                .gravityScale = 1.0f,
                                .additive = true};
    const ParticlePreset splinters{.perSquareMetre = 20.0f,
                                   .minCount = 3,
                                   .maxCount = 32,
                                   .minSpeed = 1.0f,
                                   .maxSpeed = 3.0f,
                                   .spread = 1.0f,
                                   .minLifetime = 0.8f,
                                   .maxLifetime = 1.5f,
                                   .minSize = 0.03f,
                                   .maxSize = 0.08f,
                                   .color = PackColor(170, 120, 70, 255),
                                   .colorJitter = 30,
                                   .drag = 0.6f,
                                   .gravityScale = 1.0f};
    const ParticlePreset woodDust{.perSquareMetre = 10.0f,
                                  .minCount = 2,
                                  .maxCount = 16,
                                  .minSpeed = 0.2f,
                                  .maxSpeed = 0.8f,
                                  .spread = 1.2f,
                                  .minLifetime = 1.0f,
                                  .maxLifetime = 2.0f,
                                  .minSize = 0.2f,
                                  .maxSize = 0.5f,
                                  .color = PackColor(190, 160, 120, 110),
                                  .colorJitter = 20,
                                  .drag = 1.5f,
                                  .gravityScale = -0.02f};
    const ParticlePreset shards{.perSquareMetre = 30.0f,
                                .minCount = 4,
                                .maxCount = 48,
                                .minSpeed = 1.0f,
                                .maxSpeed = 3.5f,
                                .spread = 1.0f,
                                .minLifetime = 0.6f,
                                .maxLifetime = 1.2f,
                                .minSize = 0.02f,
                                .maxSize = 0.06f,
                                .color = PackColor(210, 235, 230, 200),
                                .colorJitter = 15,
                                .drag = 0.2f,
                                .gravityScale = 1.0f};
    BreakEffectTable table;
    table[static_cast<std::size_t>(InteriorPattern::Concrete)] = {concreteDust, concreteChips};
    table[static_cast<std::size_t>(InteriorPattern::Steel)] = {sparks};
    table[static_cast<std::size_t>(InteriorPattern::Wood)] = {splinters, woodDust};
    table[static_cast<std::size_t>(InteriorPattern::Glass)] = {shards};
    table[static_cast<std::size_t>(InteriorPattern::Metal)] = {sparks};
    return table;
}

void SpawnBreakParticles(const BreakEvent& event, std::span<const ParticlePreset> presets, std::uint64_t seed,
                         std::vector<render::Particle>& out)
{
    Random random(seed);
    const Vec3 normal = NormalizeOrZero(event.normal);
    for (const ParticlePreset& preset : presets)
    {
        const auto wanted = static_cast<std::uint32_t>(std::max(preset.perSquareMetre * event.area, 0.0f));
        const std::uint32_t count = std::clamp(wanted, preset.minCount, preset.maxCount);
        // Spawn over the crack: within about half its width of the centroid.
        const float radius = 0.5f * std::sqrt(std::max(event.area, 0.0f));
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const Vec3 jitter(random.NextFloat(-1.0f, 1.0f), random.NextFloat(-1.0f, 1.0f),
                              random.NextFloat(-1.0f, 1.0f));
            const float side = (random.NextU64() & 1u) != 0 ? 1.0f : -1.0f;
            const Vec3 direction = NormalizeOrZero(normal * side + jitter * preset.spread);
            const auto darken = static_cast<float>(random.NextBelow(preset.colorJitter + 1u));
            const auto channel = [&](std::uint32_t shift)
            {
                const float value = static_cast<float>((preset.color >> shift) & 0xFFu) - darken;
                return static_cast<std::uint32_t>(std::max(value, 0.0f)) << shift;
            };
            render::Particle particle;
            particle.position = event.position + (jitter - normal * Dot(jitter, normal)) * radius;
            particle.velocity = direction * random.NextFloat(preset.minSpeed, preset.maxSpeed);
            particle.lifetime = random.NextFloat(preset.minLifetime, preset.maxLifetime);
            particle.life = particle.lifetime;
            particle.color = channel(0) | channel(8) | channel(16) | (preset.color & 0xFF000000u);
            particle.size = random.NextFloat(preset.minSize, preset.maxSize);
            particle.drag = preset.drag;
            particle.gravityScale = preset.gravityScale;
            particle.flags = preset.additive ? render::kParticleAdditive : 0u;
            out.push_back(particle);
        }
    }
}

BreakEffects::BreakEffects(BreakEffectTable table, std::uint64_t seed)
    : m_table(std::move(table))
    , m_seed(seed)
{
}

void BreakEffects::Spawn(const DestructionWorld& destruction, std::vector<render::Particle>& out)
{
    const std::span<const BreakEvent> events = destruction.BreakEvents();
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const BreakEvent& event = events[i];
        const std::vector<DestructionMaterial>& materials = destruction.Asset(event.instance).materials;
        if (event.material >= materials.size() || materials[event.material].interiorMaterialId >= m_table.size())
        {
            continue;
        }
        SpawnBreakParticles(event, m_table[materials[event.material].interiorMaterialId],
                            HashCombine(HashCombine(m_seed, m_calls), i), out);
    }
    ++m_calls;
}

} // namespace interi::destruction
