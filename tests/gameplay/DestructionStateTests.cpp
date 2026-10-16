// SPDX-License-Identifier: MIT
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/gameplay/DestructionState.h>
#include <interi/testing/ExpectNear.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using interi::Transform;
using interi::Vec3;
using interi::destruction::DestructibleAsset;
using interi::destruction::DestructibleId;
using interi::destruction::DestructionWorld;
using interi::gameplay::CaptureDestructionState;
using interi::gameplay::DestructionState;
using interi::gameplay::ReadDestructionState;
using interi::gameplay::RestoreDestructionState;
using interi::gameplay::WriteDestructionState;
using interi::testing::GameplayScene;

namespace
{

std::shared_ptr<const DestructibleAsset> WallAsset()
{
    interi::destruction::testing::GridAssetOptions options;
    options.fractureEnergy = 100.0f;
    return std::make_shared<const DestructibleAsset>(
        interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(6, 4, 1), options));
}

/// A level: ground and two walls.
struct Level
{
    explicit Level(std::shared_ptr<const DestructibleAsset> asset)
    {
        scene.AddGround();
        walls.push_back(destruction.Spawn(scene.world, scene.jobs, asset, Transform{}));
        Transform second;
        second.position = Vec3(10.0f, 0.0f, 0.0f);
        walls.push_back(destruction.Spawn(scene.world, scene.jobs, asset, second));
    }

    void Step()
    {
        scene.Step();
        destruction.Update(scene.world, scene.jobs);
    }

    /// Chunk sets of every piece, sorted (pieces' order of creation may differ).
    [[nodiscard]] std::vector<std::vector<std::uint32_t>> PieceSets(DestructibleId id) const
    {
        std::vector<std::vector<std::uint32_t>> sets;
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            const auto chunks = destruction.PieceChunks(id, piece);
            if (!chunks.empty())
            {
                sets.emplace_back(chunks.begin(), chunks.end());
            }
        }
        std::ranges::sort(sets);
        return sets;
    }

    GameplayScene scene;
    DestructionWorld destruction{scene.physics};
    std::vector<DestructibleId> walls;
};

/// Cuts the top-right 2 x 2 block (x 4..5, y 2..3) of the first wall loose with small blasts on its four boundary
/// bonds, then lets it fall for a while.
void Damage(Level& level)
{
    const Vec3 boundary[] = {{4.5f, 2.0f, 0.5f}, {5.5f, 2.0f, 0.5f}, {4.0f, 2.5f, 0.5f}, {4.0f, 3.5f, 0.5f}};
    for (const Vec3& center : boundary)
    {
        level.destruction.QueueDamage({.center = center, .energy = 1.0e5f, .innerRadius = 0.1f, .radius = 0.2f},
                                      interi::destruction::DamageKind::Explosion);
    }
    for (int i = 0; i < 20; ++i)
    {
        level.Step();
    }
}

} // namespace

TEST(DestructionState, SaveLoad_RestoresBrokenBonds)
{
    const auto asset = WallAsset();
    Level original(asset);
    Damage(original);
    const DestructionState saved =
        CaptureDestructionState(original.scene.world, original.destruction, original.scene.physics);
    ASSERT_EQ(saved.instances.size(), 2u);
    ASSERT_FALSE(saved.instances[0].pieces.empty()) << "the blast should have released a falling piece";

    const auto bytes = WriteDestructionState(saved);
    const auto loaded = ReadDestructionState(bytes);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

    Level restored(asset);
    ASSERT_TRUE(RestoreDestructionState(restored.scene.world, restored.scene.jobs, restored.destruction,
                                        restored.scene.physics, *loaded)
                    .has_value());
    for (std::size_t wall = 0; wall < 2; ++wall)
    {
        const DestructibleId a = original.walls[wall];
        const DestructibleId b = restored.walls[wall];
        const auto healthA = original.destruction.BondHealth(a);
        const auto healthB = restored.destruction.BondHealth(b);
        ASSERT_EQ(healthA.size(), healthB.size());
        for (std::uint32_t bond = 0; bond < healthA.size(); ++bond)
        {
            EXPECT_EQ(original.destruction.IsBondBroken(a, bond), restored.destruction.IsBondBroken(b, bond));
            EXPECT_EQ(healthA[bond], healthB[bond]);
        }
        EXPECT_EQ(original.PieceSets(a), restored.PieceSets(b));
    }
}

TEST(DestructionState, SaveLoad_RestoresPiecePoses)
{
    const auto asset = WallAsset();
    Level original(asset);
    Damage(original);
    const DestructionState saved =
        CaptureDestructionState(original.scene.world, original.destruction, original.scene.physics);

    Level restored(asset);
    ASSERT_TRUE(RestoreDestructionState(restored.scene.world, restored.scene.jobs, restored.destruction,
                                        restored.scene.physics, saved)
                    .has_value());
    const DestructionState again =
        CaptureDestructionState(restored.scene.world, restored.destruction, restored.scene.physics);
    ASSERT_EQ(again.instances[0].pieces.size(), saved.instances[0].pieces.size());
    for (std::size_t i = 0; i < saved.instances[0].pieces.size(); ++i)
    {
        const auto& expected = saved.instances[0].pieces[i];
        const auto& actual = again.instances[0].pieces[i];
        EXPECT_EQ(actual.firstChunk, expected.firstChunk);
        INTERI_EXPECT_VEC_NEAR(actual.pose.position, expected.pose.position, 0.0f);
        INTERI_EXPECT_QUAT_NEAR(actual.pose.rotation, expected.pose.rotation, 0.0f);
        INTERI_EXPECT_VEC_NEAR(actual.linearVelocity, expected.linearVelocity, 0.0f);
    }

    // The bodies are created at the saved pose and keep falling from there.
    restored.Step();
    const DestructionState stepped =
        CaptureDestructionState(restored.scene.world, restored.destruction, restored.scene.physics);
    const auto& before = saved.instances[0].pieces[0];
    const auto& after = stepped.instances[0].pieces[0];
    INTERI_EXPECT_VEC_NEAR(after.pose.position,
                           before.pose.position + before.linearVelocity * interi::testing::kGameplayStep, 0.05f);
}

TEST(DestructionState, RestoreIntoLiveWorld_MovesExistingBodies)
{
    Level level(WallAsset());
    Damage(level);
    const DestructionState saved = CaptureDestructionState(level.scene.world, level.destruction, level.scene.physics);
    for (int i = 0; i < 30; ++i)
    {
        level.Step(); // the pieces fall further
    }
    ASSERT_TRUE(
        RestoreDestructionState(level.scene.world, level.scene.jobs, level.destruction, level.scene.physics, saved)
            .has_value());
    const DestructionState now = CaptureDestructionState(level.scene.world, level.destruction, level.scene.physics);
    ASSERT_EQ(now.instances[0].pieces.size(), saved.instances[0].pieces.size());
    for (std::size_t i = 0; i < saved.instances[0].pieces.size(); ++i)
    {
        INTERI_EXPECT_VEC_NEAR(now.instances[0].pieces[i].pose.position, saved.instances[0].pieces[i].pose.position,
                               1e-5f);
        INTERI_EXPECT_VEC_NEAR(now.instances[0].pieces[i].linearVelocity, saved.instances[0].pieces[i].linearVelocity,
                               1e-5f);
    }
}

TEST(DestructionState, ReleasedChunks_StayGone)
{
    const auto asset = WallAsset();
    Level level(asset);
    const DestructibleId wall = level.walls[0];
    // Chunk 23 (top right corner) went to the DebrisSystem: its bonds are gone.
    std::vector<float> health(level.destruction.BondHealth(wall).begin(), level.destruction.BondHealth(wall).end());
    const auto& bonds = asset->bonds;
    for (std::uint32_t bond = 0; bond < bonds.size(); ++bond)
    {
        if (bonds[bond].chunkA == 23 || bonds[bond].chunkB == 23)
        {
            health[bond] = 0.0f;
        }
    }
    const std::vector<std::uint32_t> released{23};
    ASSERT_TRUE(level.destruction.RestoreDamage(level.scene.world, level.scene.jobs, wall, health, released));
    EXPECT_TRUE(level.destruction.IsChunkReleased(wall, 23));
    EXPECT_FALSE(level.destruction.ChunkPose(level.scene.world, wall, 23).has_value());
    for (const auto& set : level.PieceSets(wall))
    {
        EXPECT_FALSE(std::ranges::contains(set, 23u));
    }
    const DestructionState saved = CaptureDestructionState(level.scene.world, level.destruction, level.scene.physics);
    EXPECT_EQ(saved.instances[0].releasedChunks, released);
}

TEST(DestructionState, Mismatch_Fails)
{
    const auto asset = WallAsset();
    Level level(asset);
    DestructionState state = CaptureDestructionState(level.scene.world, level.destruction, level.scene.physics);

    DestructionState fewer = state;
    fewer.instances.pop_back();
    EXPECT_EQ(
        RestoreDestructionState(level.scene.world, level.scene.jobs, level.destruction, level.scene.physics, fewer)
            .error()
            .code,
        interi::ErrorCode::InvalidArgument);

    DestructionState shorter = state;
    shorter.instances[0].bondHealth.pop_back();
    EXPECT_FALSE(
        RestoreDestructionState(level.scene.world, level.scene.jobs, level.destruction, level.scene.physics, shorter)
            .has_value());

    // A bond broken in the world but alive in the save can't be repaired.
    Damage(level);
    EXPECT_FALSE(
        RestoreDestructionState(level.scene.world, level.scene.jobs, level.destruction, level.scene.physics, state)
            .has_value());
}

TEST(DestructionState, Corrupt_ReturnsError)
{
    Level level(WallAsset());
    Damage(level);
    const std::vector<std::byte> bytes =
        WriteDestructionState(CaptureDestructionState(level.scene.world, level.destruction, level.scene.physics));

    std::vector<std::byte> flipped = bytes;
    flipped[flipped.size() - 3] ^= std::byte{0x10};
    EXPECT_EQ(ReadDestructionState(flipped).error().code, interi::ErrorCode::Corrupt);

    const std::vector<std::byte> truncated(bytes.begin(),
                                           bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    EXPECT_EQ(ReadDestructionState(truncated).error().code, interi::ErrorCode::Corrupt);

    std::vector<std::byte> newer = bytes;
    newer[4] = std::byte{0xFF}; // version (little-endian u32 after the magic)
    EXPECT_EQ(ReadDestructionState(newer).error().code, interi::ErrorCode::VersionMismatch);
}
