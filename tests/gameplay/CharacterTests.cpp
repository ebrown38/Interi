// SPDX-License-Identifier: MIT
#include <interi/gameplay/Character.h>

#include "GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>

using interi::Vec3;
using interi::gameplay::CharacterController;
using interi::gameplay::CharacterInput;
using interi::gameplay::CharacterSettings;
using interi::testing::GameplayScene;
using interi::testing::kGameplayStep;

namespace
{

/// Steps the scene and the character together for `seconds`.
void Simulate(GameplayScene& scene, CharacterController& character, const CharacterInput& input, float seconds)
{
    const int steps = static_cast<int>(seconds / kGameplayStep);
    for (int i = 0; i < steps; ++i)
    {
        scene.Step();
        character.Update(kGameplayStep, input);
    }
}

constexpr CharacterInput kWalkPlusX{.move = Vec3(1.0f, 0.0f, 0.0f)};

} // namespace

TEST(Character, Gravity_LandsOnGround)
{
    GameplayScene scene;
    scene.AddGround();
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 2.0f, 0.0f));
    EXPECT_FALSE(character.OnGround());
    Simulate(scene, character, {}, 1.5f);
    EXPECT_TRUE(character.OnGround());
    EXPECT_NEAR(character.FootPosition().y, 0.0f, 0.05f);
}

TEST(Character, Walk_MovesAtWalkSpeed)
{
    GameplayScene scene;
    scene.AddGround();
    const CharacterSettings settings;
    CharacterController character(scene.physics, settings, Vec3(0.0f, 0.0f, 0.0f));
    Simulate(scene, character, {}, 0.2f);
    Simulate(scene, character, kWalkPlusX, 1.0f);
    EXPECT_NEAR(character.FootPosition().x, settings.walkSpeed * 1.0f, 0.3f);
    EXPECT_NEAR(character.FootPosition().z, 0.0f, 0.01f);
}

TEST(Character, Jump_LeavesGroundAndLands)
{
    GameplayScene scene;
    scene.AddGround();
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 0.0f, 0.0f));
    Simulate(scene, character, {}, 0.2f);
    ASSERT_TRUE(character.OnGround());
    scene.Step();
    character.Update(kGameplayStep, CharacterInput{.move = Vec3(), .jump = true});
    float apex = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        scene.Step();
        character.Update(kGameplayStep, {});
        apex = std::max(apex, character.FootPosition().y);
    }
    EXPECT_GT(apex, 0.8f);
    EXPECT_TRUE(character.OnGround());
    EXPECT_NEAR(character.FootPosition().y, 0.0f, 0.05f);
}

TEST(Character, WalkIntoDebris_PushesLightPieces)
{
    GameplayScene scene;
    scene.AddGround();
    const interi::Entity piece = scene.AddDynamicBox(Vec3(1.5f, 0.25f, 0.0f), Vec3(0.25f), 5.0f);
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 0.0f, 0.0f));
    Simulate(scene, character, kWalkPlusX, 2.0f);
    EXPECT_GT(scene.PositionOf(piece).x, 2.5f);
}

TEST(Character, WalkIntoHeavyPiece_NotPushed)
{
    GameplayScene scene;
    scene.AddGround();
    const interi::Entity piece = scene.AddDynamicBox(Vec3(1.5f, 0.5f, 0.0f), Vec3(0.5f), 5000.0f);
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 0.0f, 0.0f));
    Simulate(scene, character, kWalkPlusX, 2.0f);
    EXPECT_NEAR(scene.PositionOf(piece).x, 1.5f, 0.05f);
    EXPECT_LT(character.FootPosition().x, 1.0f);
}

TEST(Character, StepUpRubble_WithinStepHeight)
{
    GameplayScene scene;
    scene.AddGround();
    const CharacterSettings settings;
    ASSERT_GT(settings.stepHeight, 0.25f);
    scene.AddStaticBox(Vec3(2.0f, 0.125f, 0.0f), Vec3(0.5f, 0.125f, 2.0f)); // a 0.25 m ledge of rubble
    CharacterController character(scene.physics, settings, Vec3(0.0f, 0.0f, 0.0f));
    float heightOnLedge = -1.0f;
    for (int i = 0; i < 120 && character.FootPosition().x < 2.5f; ++i)
    {
        scene.Step();
        character.Update(kGameplayStep, kWalkPlusX);
        if (character.FootPosition().x > 1.8f && character.FootPosition().x < 2.2f)
        {
            heightOnLedge = character.FootPosition().y;
        }
    }
    EXPECT_NEAR(heightOnLedge, 0.25f, 0.05f);
}

TEST(Character, StepTooHigh_Blocked)
{
    GameplayScene scene;
    scene.AddGround();
    scene.AddStaticBox(Vec3(2.0f, 0.4f, 0.0f), Vec3(0.5f, 0.4f, 2.0f)); // 0.8 m: a wall, not a step
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 0.0f, 0.0f));
    Simulate(scene, character, kWalkPlusX, 1.5f);
    EXPECT_LT(character.FootPosition().x, 1.5f);
    EXPECT_NEAR(character.FootPosition().y, 0.0f, 0.05f);
}

TEST(Character, Teleport_MovesWithoutSweeping)
{
    GameplayScene scene;
    scene.AddGround();
    CharacterController character(scene.physics, CharacterSettings{}, Vec3(0.0f, 0.0f, 0.0f));
    character.Teleport(Vec3(10.0f, 0.0f, -4.0f));
    EXPECT_EQ(character.FootPosition().x, 10.0f);
    EXPECT_EQ(character.FootPosition().z, -4.0f);
    EXPECT_EQ(character.Velocity().x, 0.0f);
}
