// SPDX-License-Identifier: MIT
#pragma once

#include <interi/physics/PhysicsLayers.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/testing/StateHasher.h>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/StateRecorderImpl.h>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace interi::physics::testing
{

inline JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

/// A 100 x 2 x 100 static box whose top face is at y = 0.
inline JPH::BodyID AddGround(PhysicsWorld& world)
{
    const JPH::BodyCreationSettings settings(new JPH::BoxShape(JPH::Vec3(50.0f, 1.0f, 50.0f)),
                                             JPH::RVec3(0.0f, -1.0f, 0.0f), JPH::Quat::sIdentity(),
                                             JPH::EMotionType::Static, ToJolt(ObjectLayer::Static));
    return world.Bodies().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
}

/// Hash of Jolt's full saved state (bodies, contacts, constraints).
inline std::uint64_t HashState(const PhysicsWorld& world)
{
    JPH::StateRecorderImpl recorder;
    world.System().SaveState(recorder);
    const std::string state = recorder.GetData();
    interi::testing::StateHasher hasher;
    hasher.AddBytes(std::as_bytes(std::span<const char>(state.data(), state.size())));
    return hasher.Finish();
}

} // namespace interi::physics::testing
