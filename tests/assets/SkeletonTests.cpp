// SPDX-License-Identifier: MIT
#include <interi/assets/Skeleton.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace interi;
using namespace interi::assets;

namespace
{

Skeleton MakeSkeleton()
{
    Skeleton skeleton;
    skeleton.name = "rig";
    Joint root;
    root.name = "root";
    Joint child;
    child.name = "child";
    child.parent = 0;
    child.bind.translation = Vec3(0.0f, 1.0f, 0.0f);
    child.bind.rotation = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), 0.5f);
    child.inverseBind = Mat4::Translation(Vec3(0.0f, -1.0f, 0.0f));
    skeleton.joints = {root, child};
    return skeleton;
}

AnimationClip MakeClip()
{
    AnimationClip clip;
    clip.name = "wave";
    clip.duration = 1.0f;
    AnimationChannel translation;
    translation.joint = 1;
    translation.times = {0.0f, 1.0f};
    translation.values = {Vec4(0, 0, 0, 0), Vec4(0, 2, 0, 0)};
    AnimationChannel rotation;
    rotation.joint = 0;
    rotation.path = ChannelPath::Rotation;
    rotation.interpolation = Interpolation::Step;
    rotation.times = {0.5f};
    rotation.values = {Vec4(0, 0, 0, 1)};
    clip.channels = {translation, rotation};
    return clip;
}

} // namespace

TEST(Skeleton, WriteThenRead_RoundTrips)
{
    const Skeleton skeleton = MakeSkeleton();
    auto read = ReadSkeletonAsset(WriteSkeletonAsset(skeleton));
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, skeleton);
}

TEST(Skeleton, ParentAfterChild_FailsValidation)
{
    Skeleton skeleton = MakeSkeleton();
    skeleton.joints[0].parent = 1;
    EXPECT_FALSE(ValidateSkeleton(skeleton).has_value());
    EXPECT_FALSE(ReadSkeletonAsset(WriteSkeletonAsset(skeleton)).has_value());
}

TEST(Skeleton, NonFiniteBindPose_FailsValidation)
{
    Skeleton skeleton = MakeSkeleton();
    skeleton.joints[1].bind.scale.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(ValidateSkeleton(skeleton).has_value());
}

TEST(Skeleton, FlippedByte_IsCorrupt)
{
    std::vector<std::byte> bytes = WriteSkeletonAsset(MakeSkeleton());
    bytes[bytes.size() / 2] ^= std::byte{0x10};
    auto read = ReadSkeletonAsset(bytes);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, ErrorCode::Corrupt);
}

TEST(Clip, WriteThenRead_RoundTrips)
{
    const AnimationClip clip = MakeClip();
    auto read = ReadClipAsset(WriteClipAsset(clip), 2);
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, clip);
}

TEST(Clip, JointOutOfRange_Rejected)
{
    EXPECT_FALSE(ReadClipAsset(WriteClipAsset(MakeClip()), 1).has_value());
}

TEST(Clip, KeysOutOfOrder_Rejected)
{
    AnimationClip clip = MakeClip();
    clip.channels[0].times = {0.5f, 0.25f};
    EXPECT_FALSE(ValidateClip(clip, 2).has_value());
}

TEST(Clip, KeyBeyondDuration_Rejected)
{
    AnimationClip clip = MakeClip();
    clip.duration = 0.5f;
    EXPECT_FALSE(ValidateClip(clip, 2).has_value());
}

TEST(Clip, NonUnitRotation_Rejected)
{
    AnimationClip clip = MakeClip();
    clip.channels[1].values[0] = Vec4(0, 0, 0, 2);
    EXPECT_FALSE(ValidateClip(clip, 2).has_value());
}

TEST(Clip, ValueCountMismatch_Rejected)
{
    AnimationClip clip = MakeClip();
    clip.channels[0].values.pop_back();
    EXPECT_FALSE(ValidateClip(clip, 2).has_value());
}

TEST(Skin, WriteThenRead_RoundTrips)
{
    SkinBinding skin;
    skin.skeleton = "rigs/hero.iskel";
    SkinInfluence influence;
    influence.joints = {0, 1, 0, 0};
    influence.weights = {0.25f, 0.75f, 0.0f, 0.0f};
    skin.influences = {influence, SkinInfluence{}};
    auto read = ReadSkinAsset(WriteSkinAsset(skin), 2);
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, skin);
}

TEST(Skin, WeightsNotSummingToOne_Rejected)
{
    SkinBinding skin;
    SkinInfluence influence;
    influence.weights = {0.5f, 0.0f, 0.0f, 0.0f};
    skin.influences = {influence};
    EXPECT_FALSE(ValidateSkin(skin, 2).has_value());
}

TEST(Skin, JointOutOfRange_Rejected)
{
    SkinBinding skin;
    SkinInfluence influence;
    influence.joints = {5, 0, 0, 0};
    influence.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    skin.influences = {influence};
    EXPECT_FALSE(ValidateSkin(skin, 2).has_value());
}
