// SPDX-License-Identifier: MIT
#pragma once

// Scene trigger volumes (physics-free; see runtime/TriggerVolumes.h). An object with a TriggerVolumeComponent is a
// box or sphere (at its world transform's position; axis-aligned, scale multiplies the size); objects with a
// TriggerActorComponent are what it detects. SceneRuntime::Step raises, after the chains ran:
//   trigger.Overlap     the step after an actor enters a volume     (entity: the volume, key: its tag, payload: other)
//   trigger.OverlapEnd  the step after it leaves (or is destroyed)  (same)
// Events are ordered by (volume id, actor id), so they are identical at any thread count.

#include <interi/ecs/World.h>
#include <interi/runtime/TriggerVolumes.h>
#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <string>
#include <vector>

namespace interi::scene
{

struct TriggerVolumeComponent
{
    static constexpr const char* kComponentName = "interi.TriggerVolume";
    values::EnumValue shape{"Box"}; ///< "Box" or "Sphere".
    Vec3 halfExtents{0.5f};
    float radius = 0.5f;
    std::string tag;          ///< Matched against a trigger node's "key" input.
    std::uint32_t mask = ~0u; ///< Actor layers it detects.
};

struct TriggerActorComponent
{
    static constexpr const char* kComponentName = "interi.TriggerActor";
    float radius = 0.0f;
    std::uint32_t layers = 1u;
};

void RegisterTriggerReflection(values::ReflectionRegistry& registry);

/// The volumes and actors of `world` (objects with a StableIdComponent and a world Transform), in id order.
struct TriggerScan
{
    std::vector<TriggerVolume> volumes;
    std::vector<TriggerActor> actors;
    std::vector<std::string> tags; ///< By volume index.
};
[[nodiscard]] TriggerScan ScanTriggers(World& world);

} // namespace interi::scene
