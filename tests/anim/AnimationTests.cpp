// SPDX-License-Identifier: MIT
#include "AnimTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::anim;
using namespace interi::anim::testing;

TEST(Animation, SampleClip_InterpolatesPositionLinearly)
{
    const Skeleton skeleton = ArmSkeleton();
    Pose pose;
    SampleClip(skeleton, RaiseClip(), 0.25f, false, pose);
    EXPECT_EQ(pose[1].position, Vec3(0.0f, 1.5f, 0.0f));
    EXPECT_EQ(pose[0].position, Vec3{});
}

TEST(Animation, SampleClip_OneShotClampsLoopWraps)
{
    const Skeleton skeleton = ArmSkeleton();
    Pose pose;
    SampleClip(skeleton, RaiseClip(), 5.0f, false, pose);
    EXPECT_EQ(pose[1].position, Vec3(0.0f, 3.0f, 0.0f));
    SampleClip(skeleton, RaiseClip(), 1.25f, true, pose);
    EXPECT_EQ(pose[1].position, Vec3(0.0f, 1.5f, 0.0f));
}

TEST(Animation, SampleClip_RotationUsesNlerpAndStaysUnit)
{
    const Skeleton skeleton = ArmSkeleton();
    Clip clip = HoldClip("turn");
    clip.tracks.push_back(
        {.joint = 0,
         .position = {},
         .rotation = {{.time = 0.0f, .value = Quat{}}, {.time = 1.0f, .value = Quat{0.0f, 1.0f, 0.0f, 0.0f}}},
         .scale = {}});
    Pose pose;
    SampleClip(skeleton, clip, 0.5f, false, pose);
    const Quat q = pose[0].rotation;
    EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-6f);
    EXPECT_NEAR(q.y, q.w, 1e-6f);
}

TEST(Animation, BlendPoses_EndpointsAreExact)
{
    const Skeleton skeleton = ArmSkeleton();
    Pose a;
    Pose b;
    SampleClip(skeleton, RaiseClip(), 0.0f, false, a);
    SampleClip(skeleton, RaiseClip(), 1.0f, false, b);
    Pose out;
    BlendPoses(a, b, 0.0f, out);
    EXPECT_EQ(out[1].position, a[1].position);
    BlendPoses(a, b, 1.0f, out);
    EXPECT_EQ(out[1].position, b[1].position);
    BlendPoses(a, b, 0.5f, out);
    EXPECT_EQ(out[1].position, Vec3(0.0f, 2.0f, 0.0f));
}

TEST(Animation, SkinMatrices_BindPoseIsIdentity)
{
    const Skeleton skeleton = ArmSkeleton();
    const std::vector<Mat4> skin = ComputeSkinMatrices(skeleton, BindPose(skeleton));
    ASSERT_EQ(skin.size(), 2u);
    for (const Mat4& m : skin)
    {
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                EXPECT_NEAR(m.columns[c][r], c == r ? 1.0f : 0.0f, 1e-6f);
            }
        }
    }
}

TEST(Animation, ModelPose_ChildFollowsParent)
{
    const Skeleton skeleton = ArmSkeleton();
    Pose pose = BindPose(skeleton);
    pose[0].position = Vec3(5.0f, 0.0f, 0.0f);
    const std::vector<Mat4> model = ComputeModelPose(skeleton, pose);
    const Vec3 elbow = TransformPoint(model[1], Vec3{});
    EXPECT_EQ(elbow, Vec3(5.0f, 1.0f, 0.0f));
}

TEST(Animation, SkeletonJson_RoundTrips)
{
    const Skeleton skeleton = ArmSkeleton();
    auto parsed = ParseSkeleton(WriteSkeleton(skeleton));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, skeleton);
}

TEST(Animation, ClipJson_RoundTrips)
{
    const Skeleton skeleton = ArmSkeleton();
    const Clip clip = RaiseClip();
    auto parsed = ParseClip(WriteClip(clip, skeleton), skeleton);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, clip);
}

TEST(Animation, ParseSkeleton_ChildBeforeParent_Rejected)
{
    const auto parsed = ParseSkeleton(R"({"version":1,"joints":[{"name":"a","parent":1},{"name":"b"}]})");
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, ErrorCode::InvalidArgument);
}

TEST(Animation, ParseSkeleton_DuplicateNameOrBadVersion_Rejected)
{
    EXPECT_FALSE(ParseSkeleton(R"({"version":1,"joints":[{"name":"a"},{"name":"a"}]})").has_value());
    const auto version = ParseSkeleton(R"({"version":9,"joints":[{"name":"a"}]})");
    ASSERT_FALSE(version.has_value());
    EXPECT_EQ(version.error().code, ErrorCode::VersionMismatch);
    EXPECT_FALSE(ParseSkeleton("not json").has_value());
}

TEST(Animation, ParseClip_Malformed_Rejected)
{
    const Skeleton skeleton = ArmSkeleton();
    EXPECT_FALSE(ParseClip(R"({"version":1,"duration":1,"tracks":[{"joint":"ghost"}]})", skeleton).has_value());
    EXPECT_FALSE(ParseClip(R"({"version":1,"duration":0})", skeleton).has_value());
    // Key times must increase and stay inside the duration.
    EXPECT_FALSE(ParseClip(R"({"version":1,"duration":1,"tracks":[{"joint":"elbow",
        "position":[[0.5,0,0,0],[0.5,1,1,1]]}]})",
                           skeleton)
                     .has_value());
    EXPECT_FALSE(ParseClip(R"({"version":1,"duration":1,"tracks":[{"joint":"elbow",
        "position":[[0,0,0,0],[2,1,1,1]]}]})",
                           skeleton)
                     .has_value());
    EXPECT_FALSE(ParseClip(R"({"version":1,"duration":1,"tracks":[{"joint":"elbow",
        "rotation":[[0,0,0,0,0]]}]})",
                           skeleton)
                     .has_value());
}
