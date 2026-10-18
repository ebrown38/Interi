// SPDX-License-Identifier: MIT
#include <interi/runtime/CameraRig.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using namespace interi;

namespace
{

constexpr const char* kRigs = R"json({
  "rigs": [
    { "name": "orbit", "kind": "orbit", "distance": 5, "shoulder": 0, "fov": 60 },
    { "name": "eyes", "kind": "firstPerson", "eyeHeight": 0.5 },
    { "name": "cctv", "kind": "fixed", "position": [0, 6, 8], "lookAt": [0, 0, 0] },
    { "name": "map", "kind": "topDown", "height": 10, "pitch": -1.5 }
  ]
})json";

CameraRigSet Load()
{
    auto set = ParseCameraRigs(kRigs);
    EXPECT_TRUE(set.has_value());
    return set.value_or(CameraRigSet{});
}

} // namespace

TEST(CameraRig, Parse_ReadsFourKinds)
{
    const CameraRigSet set = Load();
    ASSERT_EQ(set.rigs.size(), 4u);
    EXPECT_EQ(set.rigs[0].kind, CameraRigKind::Orbit);
    EXPECT_EQ(set.rigs[1].kind, CameraRigKind::FirstPerson);
    EXPECT_EQ(set.rigs[2].kind, CameraRigKind::Fixed);
    EXPECT_EQ(set.rigs[3].kind, CameraRigKind::TopDown);
    EXPECT_FLOAT_EQ(set.rigs[0].fov, 60.0f);
    EXPECT_TRUE(set.warnings.empty());
}

TEST(CameraRig, Parse_Malformed_Fails)
{
    EXPECT_FALSE(ParseCameraRigs("[]"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"kind":"orbit"}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"nope"}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"orbit","fov":500}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"topDown","pitch":0.5}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"fixed","position":[1,1,1],"lookAt":[1,1,1]}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"orbit"},{"name":"a","kind":"orbit"}]})"));
    EXPECT_FALSE(ParseCameraRigs(R"({"rigs":[{"name":"a","kind":"orbit","distance":"far"}]})"));
}

TEST(CameraRig, Parse_UnknownFields_Warn)
{
    const auto set = ParseCameraRigs(R"({"extra":1,"rigs":[{"name":"a","kind":"orbit","zoom":2}]})");
    ASSERT_TRUE(set.has_value());
    EXPECT_EQ(set->warnings.size(), 2u);
}

TEST(CameraRig, Orbit_SitsBehindTarget)
{
    const CameraRigSet set = Load();
    const CameraPose pose = EvaluateRig(set.rigs[0], 0.0f, 0.0f, Vec3(0.0f, 1.0f, 0.0f));
    INTERI_EXPECT_VEC_NEAR(pose.eye, Vec3(0.0f, 1.0f, 5.0f), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(pose.forward, Vec3(0.0f, 0.0f, -1.0f), 1e-6f);
}

TEST(CameraRig, FirstPerson_IsAtTheTarget)
{
    const CameraRigSet set = Load();
    const CameraPose pose = EvaluateRig(set.rigs[1], 0.0f, 0.0f, Vec3(2.0f, 1.0f, 3.0f));
    INTERI_EXPECT_VEC_NEAR(pose.eye, Vec3(2.0f, 1.5f, 3.0f), 1e-6f);
}

TEST(CameraRig, Fixed_IgnoresTargetAndLooksAtPoint)
{
    const CameraRigSet set = Load();
    const CameraPose pose = EvaluateRig(set.rigs[2], 1.0f, 0.3f, Vec3(50.0f, 0.0f, 0.0f));
    INTERI_EXPECT_VEC_NEAR(pose.eye, Vec3(0.0f, 6.0f, 8.0f), 1e-6f);
    INTERI_EXPECT_VEC_NEAR(pose.forward, Normalize(Vec3(0.0f, -6.0f, -8.0f)), 1e-6f);
}

TEST(CameraRig, TopDown_HoversAboveTarget)
{
    const CameraRigSet set = Load();
    const CameraPose pose = EvaluateRig(set.rigs[3], 0.0f, 0.7f, Vec3(4.0f, 0.0f, 4.0f));
    EXPECT_NEAR(pose.eye.y, 10.0f, 1e-4f); // height above the target
    EXPECT_LT(pose.forward.y, -0.99f);
}

TEST(CameraController, SwitchTo_RaisesEventAndBlends)
{
    CameraController camera(Load().rigs, 1.0f);
    EXPECT_TRUE(camera.SwitchTo("cctv"));
    const auto events = camera.TakeEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].from, "orbit");
    EXPECT_EQ(events[0].to, "cctv");
    EXPECT_TRUE(camera.TakeEvents().empty());

    const Vec3 target(0.0f, 1.0f, 0.0f);
    camera.SetView(0.0f, 0.0f);
    const CameraPose start = camera.Pose(target);
    INTERI_EXPECT_VEC_NEAR(start.eye, Vec3(0.0f, 1.0f, 5.0f), 1e-5f); // still the orbit rig
    camera.Update(0.5f);
    const CameraPose mid = camera.Pose(target);
    INTERI_EXPECT_VEC_NEAR(mid.eye, Vec3(0.0f, 3.5f, 6.5f), 1e-4f);
    camera.Update(0.5f);
    EXPECT_FALSE(camera.Blending());
    INTERI_EXPECT_VEC_NEAR(camera.Pose(target).eye, Vec3(0.0f, 6.0f, 8.0f), 1e-5f);
}

TEST(CameraController, SwitchTo_UnknownOrCurrent_NoEvent)
{
    CameraController camera(Load().rigs);
    EXPECT_FALSE(camera.SwitchTo("nope"));
    EXPECT_TRUE(camera.SwitchTo("orbit"));
    EXPECT_TRUE(camera.TakeEvents().empty());
    EXPECT_EQ(camera.ActiveIndex(), 0u);
}

TEST(CameraController, SwitchNext_Wraps)
{
    CameraController camera(Load().rigs, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
        camera.SwitchNext();
    }
    EXPECT_EQ(camera.ActiveIndex(), 0u);
    EXPECT_EQ(camera.TakeEvents().size(), 4u);
    EXPECT_FALSE(camera.Blending());
}

TEST(CameraController, Rotate_ClampsToRigPitchLimits)
{
    CameraController camera(Load().rigs);
    camera.Rotate(0.0f, -100000.0f);
    EXPECT_FLOAT_EQ(camera.Pitch(), 1.2f);
    camera.Rotate(0.0f, 100000.0f);
    EXPECT_FLOAT_EQ(camera.Pitch(), -1.4f);
}

TEST(CameraController, NoRigs_ReturnsDefaultPose)
{
    CameraController camera;
    EXPECT_FALSE(camera.SwitchTo("a"));
    EXPECT_EQ(camera.Active(), nullptr);
    camera.Rotate(1.0f, 1.0f);
    EXPECT_FLOAT_EQ(camera.Pose({}).fov, 70.0f);
}
