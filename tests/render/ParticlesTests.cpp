// SPDX-License-Identifier: MIT
#include <interi/render/Particles.h>

#include <gtest/gtest.h>

#include <vector>

using namespace interi;
using namespace interi::render;

TEST(Particles, Integrate_GravityAndDrag_SemiImplicitEuler)
{
    std::vector<Particle> particles(1);
    particles[0].position = {0.0f, 10.0f, 0.0f};
    particles[0].velocity = {1.0f, 0.0f, 0.0f};
    particles[0].life = 2.0f;
    particles[0].drag = 0.5f;
    IntegrateParticles(particles, {0.0f, -10.0f, 0.0f}, 0.1f);
    // v = (1, -1, 0) * (1 - 0.05); p += v * 0.1.
    EXPECT_FLOAT_EQ(particles[0].velocity.x, 0.95f);
    EXPECT_FLOAT_EQ(particles[0].velocity.y, -0.95f);
    EXPECT_FLOAT_EQ(particles[0].position.x, 0.095f);
    EXPECT_FLOAT_EQ(particles[0].position.y, 9.905f);
    EXPECT_FLOAT_EQ(particles[0].life, 1.9f);
}

TEST(Particles, Integrate_DeadParticles_Untouched)
{
    std::vector<Particle> particles(1);
    particles[0].position = {1.0f, 2.0f, 3.0f};
    particles[0].velocity = {4.0f, 5.0f, 6.0f};
    particles[0].life = 0.0f;
    IntegrateParticles(particles, {0.0f, -10.0f, 0.0f}, 0.1f);
    EXPECT_EQ(particles[0].position.y, 2.0f);
    EXPECT_EQ(particles[0].velocity.y, 5.0f);
}

TEST(Particles, Integrate_NegativeGravityScale_Rises)
{
    std::vector<Particle> particles(1);
    particles[0].life = 1.0f;
    particles[0].gravityScale = -0.2f; // smoke
    IntegrateParticles(particles, {0.0f, -10.0f, 0.0f}, 0.5f);
    EXPECT_GT(particles[0].position.y, 0.0f);
}

TEST(Particles, Integrate_HugeDrag_StopsWithoutReversing)
{
    std::vector<Particle> particles(1);
    particles[0].life = 1.0f;
    particles[0].velocity = {3.0f, 0.0f, 0.0f};
    particles[0].drag = 100.0f;
    particles[0].gravityScale = 0.0f;
    IntegrateParticles(particles, {0.0f, -10.0f, 0.0f}, 0.1f);
    EXPECT_EQ(particles[0].velocity.x, 0.0f);
}

TEST(Particles, PackColor_RedInLowByte)
{
    EXPECT_EQ(PackColor(0x11, 0x22, 0x33, 0x44), 0x44332211u);
}
