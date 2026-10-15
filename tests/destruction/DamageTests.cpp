// SPDX-License-Identifier: MIT
#include <interi/destruction/Damage.h>
#include <interi/destruction/DestructibleAsset.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

using namespace interi::destruction;
using interi::Vec3;

/// Two bonds side by side, 1 m^2 each: bond 0 between concrete chunks, bond 1 between steel chunks.
DestructibleAsset ConcreteAndSteel()
{
    DestructibleAsset asset;
    DestructionMaterial concrete;
    concrete.name = "concrete";
    concrete.fractureEnergy = 100.0f;
    DestructionMaterial steel;
    steel.name = "steel";
    steel.fractureEnergy = 1.0e5f;
    asset.materials = {concrete, steel};
    asset.chunks.resize(4);
    asset.chunks[2].material = 1;
    asset.chunks[3].material = 1;
    Bond a;
    a.chunkA = 0;
    a.chunkB = 1;
    a.area = 1.0f;
    a.centroid = {0.0f, 0.0f, 0.0f};
    Bond b = a;
    b.chunkA = 2;
    b.chunkB = 3;
    b.centroid = {0.0f, 0.0f, 1.0f};
    asset.bonds = {a, b};
    return asset;
}

} // namespace

TEST(RadialDamage, Falloff_InverseSquareClamp)
{
    EXPECT_EQ(RadialFalloff(0.0f, 1.0f, 10.0f), 1.0f);
    EXPECT_EQ(RadialFalloff(1.0f, 1.0f, 10.0f), 1.0f);
    EXPECT_FLOAT_EQ(RadialFalloff(2.0f, 1.0f, 10.0f), 0.25f);
    EXPECT_FLOAT_EQ(RadialFalloff(4.0f, 1.0f, 10.0f), 1.0f / 16.0f);
    EXPECT_EQ(RadialFalloff(10.0f, 1.0f, 10.0f), 0.0f);
    EXPECT_EQ(RadialFalloff(20.0f, 1.0f, 10.0f), 0.0f);
}

TEST(RadialDamage, OnlyBondsWithinRadiusDamaged)
{
    const std::vector<Vec3> centroids{{0, 0, 0}, {2, 0, 0}, {6, 0, 0}};
    std::vector<float> health{100.0f, 100.0f, 100.0f};
    std::vector<std::uint32_t> broken;
    ApplyRadialDamage(centroids, {.center = {0, 0, 0}, .energy = 40.0f, .innerRadius = 1.0f, .radius = 5.0f}, 1.0f,
                      health, broken);
    EXPECT_FLOAT_EQ(health[0], 60.0f);
    EXPECT_FLOAT_EQ(health[1], 90.0f); // 40 * (1/2)^2
    EXPECT_EQ(health[2], 100.0f);
    EXPECT_TRUE(broken.empty());
}

TEST(RadialDamage, HealthReachesZero_BondReportedOnce)
{
    const std::vector<Vec3> centroids{{0, 0, 0}, {0.5f, 0, 0}};
    std::vector<float> health{10.0f, 50.0f};
    std::vector<std::uint32_t> broken;
    const RadialDamage blast{.center = {0, 0, 0}, .energy = 30.0f, .innerRadius = 1.0f, .radius = 5.0f};
    ApplyRadialDamage(centroids, blast, 1.0f, health, broken);
    EXPECT_EQ(broken, std::vector<std::uint32_t>{0});
    EXPECT_EQ(health[0], 0.0f);
    ApplyRadialDamage(centroids, blast, 1.0f, health, broken);
    EXPECT_EQ(broken, (std::vector<std::uint32_t>{0, 1}));
    EXPECT_EQ(health[1], 0.0f);
}

TEST(RadialDamage, Scale_MultipliesEnergy)
{
    const std::vector<Vec3> centroids{{0, 0, 0}};
    std::vector<float> health{100.0f};
    std::vector<std::uint32_t> broken;
    ApplyRadialDamage(centroids, {.center = {}, .energy = 10.0f, .innerRadius = 1.0f, .radius = 2.0f}, 3.0f, health,
                      broken);
    EXPECT_FLOAT_EQ(health[0], 70.0f);
}

TEST(ImpactDamage, BelowThreshold_NoBondDamage)
{
    DamageSettings settings;
    settings.impactThreshold = 1000.0f;
    const RadialDamage weak = ImpactDamage({0, 0, 0}, 999.0f, settings);
    EXPECT_EQ(weak.energy, 0.0f);
    const std::vector<Vec3> centroids{{0, 0, 0}};
    std::vector<float> health{1.0f};
    std::vector<std::uint32_t> broken;
    ApplyRadialDamage(centroids, weak, 1.0f, health, broken);
    EXPECT_EQ(health[0], 1.0f);

    const RadialDamage strong = ImpactDamage({0, 0, 0}, 1500.0f, settings);
    EXPECT_FLOAT_EQ(strong.energy, 500.0f * settings.impactEnergyPerImpulse);
    EXPECT_EQ(strong.radius, settings.impactRadius);
}

TEST(Damage, MaterialMultiplier_SteelResistsMore)
{
    const DestructibleAsset asset = ConcreteAndSteel();
    std::vector<float> health = InitialBondHealth(asset);
    ASSERT_EQ(health.size(), 2u);
    EXPECT_FLOAT_EQ(health[0], 100.0f);
    EXPECT_FLOAT_EQ(health[1], 1.0e5f);
    const std::vector<Vec3> centroids{asset.bonds[0].centroid, asset.bonds[1].centroid};
    std::vector<std::uint32_t> broken;
    // Both bonds get the full 500 J: concrete fails, steel barely notices.
    ApplyRadialDamage(centroids, {.center = {0, 0, 0.5f}, .energy = 500.0f, .innerRadius = 1.0f, .radius = 5.0f}, 1.0f,
                      health, broken);
    EXPECT_EQ(broken, std::vector<std::uint32_t>{0});
    EXPECT_GT(health[1], 0.99f * 1.0e5f);
}

TEST(Damage, InitialBondHealth_UsesWeakerMaterial)
{
    DestructibleAsset asset = ConcreteAndSteel();
    asset.bonds[0].chunkB = 2; // concrete to steel
    asset.bonds[0].area = 2.0f;
    const std::vector<float> health = InitialBondHealth(asset);
    EXPECT_FLOAT_EQ(health[0], 200.0f);
}
