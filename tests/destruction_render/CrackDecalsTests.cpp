// SPDX-License-Identifier: MIT
#include <interi/destruction_render/CrackDecals.h>

#include <gtest/gtest.h>

using namespace interi::destruction;

TEST(CrackOverlay, BondHealth_MapsToIntensity)
{
    EXPECT_EQ(CrackOverlayIntensity(100.0f, 100.0f), 0.0f); // undamaged
    EXPECT_EQ(CrackOverlayIntensity(92.0f, 100.0f), 0.0f);  // scratches do not show
    EXPECT_NEAR(CrackOverlayIntensity(50.0f, 100.0f), 0.5f, 1e-6f);
    EXPECT_EQ(CrackOverlayIntensity(5.0f, 100.0f), 1.0f);
    EXPECT_EQ(CrackOverlayIntensity(0.0f, 100.0f), 1.0f);
    float previous = 0.0f;
    for (int health = 100; health >= 0; --health)
    {
        const float intensity = CrackOverlayIntensity(static_cast<float>(health), 100.0f);
        EXPECT_GE(intensity, previous) << health;
        previous = intensity;
    }
}

TEST(CrackOverlay, NoFullHealth_NoCrack)
{
    EXPECT_EQ(CrackOverlayIntensity(0.0f, 0.0f), 0.0f);
    EXPECT_EQ(CrackOverlayIntensity(-1.0f, -1.0f), 0.0f);
}

TEST(CrackOverlay, HealthOutOfRange_Clamped)
{
    EXPECT_EQ(CrackOverlayIntensity(150.0f, 100.0f), 0.0f);
    EXPECT_EQ(CrackOverlayIntensity(-20.0f, 100.0f), 1.0f);
}
