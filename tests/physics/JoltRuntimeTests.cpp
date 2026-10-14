// SPDX-License-Identifier: MIT
#include <interi/physics/JoltRuntime.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
// clang-format on

#include <gtest/gtest.h>

using interi::physics::JoltRuntime;

TEST(JoltRuntime, Environment_InitializesJolt)
{
    EXPECT_TRUE(JoltRuntime::IsInitialized());
    ASSERT_NE(JPH::Factory::sInstance, nullptr);
    // Shapes need registered types and Jolt's allocator.
    const JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(JPH::Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(box->GetSubType(), JPH::EShapeSubType::Box);
}
