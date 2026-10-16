// SPDX-License-Identifier: MIT
#include <interi/destruction_render/BreakEffects.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace interi;
using namespace interi::destruction;

namespace
{

BreakEvent Event(float area)
{
    BreakEvent event;
    event.position = {1.0f, 2.0f, 3.0f};
    event.normal = {0.0f, 0.0f, 1.0f};
    event.area = area;
    event.energy = 100.0f * area;
    return event;
}

const std::vector<ParticlePreset>& Presets(InteriorPattern look)
{
    static const BreakEffectTable kTable = DefaultBreakEffects();
    return kTable[static_cast<std::size_t>(look)];
}

} // namespace

TEST(ParticleSpawner, BreakEvent_EmitsMaterialPreset)
{
    // Steel throws off additive sparks only; concrete alpha-blended dust and chips, no sparks.
    std::vector<render::Particle> steel;
    SpawnBreakParticles(Event(0.5f), Presets(InteriorPattern::Steel), 7, steel);
    ASSERT_FALSE(steel.empty());
    for (const render::Particle& p : steel)
    {
        EXPECT_EQ(p.flags & render::kParticleAdditive, render::kParticleAdditive);
        EXPECT_LT(p.lifetime, 1.0f); // short-lived
        EXPECT_GE(Length(p.velocity), 3.0f * 0.999f);
    }

    std::vector<render::Particle> concrete;
    SpawnBreakParticles(Event(0.5f), Presets(InteriorPattern::Concrete), 7, concrete);
    ASSERT_FALSE(concrete.empty());
    std::size_t rising = 0;
    for (const render::Particle& p : concrete)
    {
        EXPECT_EQ(p.flags & render::kParticleAdditive, 0u);
        EXPECT_EQ(p.life, p.lifetime);
        rising += p.gravityScale < 0.0f ? 1u : 0u; // the dust hangs in the air
    }
    EXPECT_GT(rising, 0u);
    EXPECT_LT(rising, concrete.size()); // and chips fall
}

TEST(ParticleSpawner, Count_ScalesWithAreaWithinLimits)
{
    const std::vector<ParticlePreset> presets{ParticlePreset{.perSquareMetre = 10.0f, .minCount = 3, .maxCount = 20}};
    std::vector<render::Particle> out;
    SpawnBreakParticles(Event(0.01f), presets, 1, out);
    EXPECT_EQ(out.size(), 3u); // min
    out.clear();
    SpawnBreakParticles(Event(1.0f), presets, 1, out);
    EXPECT_EQ(out.size(), 10u);
    out.clear();
    SpawnBreakParticles(Event(50.0f), presets, 1, out);
    EXPECT_EQ(out.size(), 20u); // max
}

TEST(ParticleSpawner, Particles_StartOnTheCrackAndLeaveIt)
{
    std::vector<render::Particle> out;
    SpawnBreakParticles(Event(1.0f), Presets(InteriorPattern::Concrete), 3, out);
    std::size_t front = 0;
    for (const render::Particle& p : out)
    {
        // In the crack plane (z = 3), within the bond's extent.
        EXPECT_NEAR(p.position.z, 3.0f, 1e-5f);
        EXPECT_LE(Length(p.position - Vec3(1.0f, 2.0f, 3.0f)), 0.5f * std::sqrt(2.0f) + 1e-4f);
        front += p.velocity.z > 0.0f ? 1u : 0u;
    }
    // Thrown to both sides.
    EXPECT_GT(front, 0u);
    EXPECT_LT(front, out.size());
}

TEST(ParticleSpawner, SameSeed_SameParticles)
{
    std::vector<render::Particle> a;
    std::vector<render::Particle> b;
    std::vector<render::Particle> c;
    SpawnBreakParticles(Event(0.3f), Presets(InteriorPattern::Wood), 11, a);
    SpawnBreakParticles(Event(0.3f), Presets(InteriorPattern::Wood), 11, b);
    SpawnBreakParticles(Event(0.3f), Presets(InteriorPattern::Wood), 12, c);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].position.x, b[i].position.x);
        EXPECT_EQ(a[i].velocity.y, b[i].velocity.y);
        EXPECT_EQ(a[i].color, b[i].color);
    }
    EXPECT_NE(a[0].position.x, c[0].position.x);
}

TEST(ParticleSpawner, DefaultTable_EveryLookHasEffects)
{
    const BreakEffectTable table = DefaultBreakEffects();
    for (const std::vector<ParticlePreset>& presets : table)
    {
        EXPECT_FALSE(presets.empty());
    }
}
