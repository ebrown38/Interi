// SPDX-License-Identifier: MIT
#pragma once

// The engine's own Chain Reaction library (physics-free): triggers, flow events and world events. Every id is a
// shared contract id the engine declares (CoreContracts), so a module may provide a better one; the highest
// provider wins.
//
// Triggers (fired by the executor; an input named "key" selects which raised key a node reacts to):
//   trigger.Start         once, on the first step                                  -> Fired
//   trigger.Tick          every step                                               -> Fired {step: int, dt: float}
//   trigger.InputAction   key: the action name (raised by the host)                -> Fired
//   trigger.Timer         interval (s): every interval, from the start             -> Fired {count: int}
//   trigger.ValueChanged  key: a Value id; the step after a chain changed it       -> Fired
//   trigger.Spawned       an entity was spawned (raised by the host)               -> Fired {entity: entity}
//   trigger.Destroyed     an entity was destroyed (world.Destroy, or the host)     -> Fired {entity: entity}
//   trigger.Overlap       key: a trigger volume's tag; an actor entered it         -> Fired {other: entity}
//   trigger.OverlapEnd    the same, when the actor left                            -> Fired {other: entity}
//                         (raised by the host for the volume's entity; see scene/Triggers.h)
//
// Flow:
//   flow.Delay     seconds                       latent -> Finished (after seconds, rounded up to whole steps)
//   flow.Timer     interval (s), count           latent -> Fired {count: int} (count times, every interval)
//   flow.Branch    condition                            -> True | False
//   flow.Sequence                                       -> Then0, Then1, Then2, Then3 (fired in that order)
//   flow.Random    min, max (int, inclusive)            -> Out {value: int} (seeded per activation: deterministic)
//
// World (applied at the end of the step, in activation order; a target of 0 means the activation's entity; entities
// are found by StableIdComponent):
//   world.SetValue     id, value (the Value's type)        -> Out   last write in a step wins
//   world.ModifyValue  id, delta (the Value's type)        -> Out   deltas accumulate
//   world.Destroy      target                              -> Out   raises trigger.Destroyed next step
//   world.Move         target, delta (vec3)                -> Out   deltas accumulate
//   world.Rotate       target, axis (vec3), degrees        -> Out   uses sin/cos: not bit-identical across platforms
//   world.Show/Hide    target                              -> Out   removes/adds HiddenComponent
//   world.PlaySound    cue, target, volume                 -> Out   ChainExecutor::Sounds() for the host
//   world.SpawnPrefab  prefab (asset), position, rotation  -> Out   ChainExecutor::Spawns() for the host, which
//                                                                   raises trigger.Spawned for the new root
// Spawning prefabs arrives with prefabs (M16).

#include <interi/chain/Event.h>
#include <interi/core/Result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::chain
{

inline constexpr std::string_view kTriggerStart = "trigger.Start";
inline constexpr std::string_view kTriggerTick = "trigger.Tick";
inline constexpr std::string_view kTriggerInputAction = "trigger.InputAction";
inline constexpr std::string_view kTriggerTimer = "trigger.Timer";
inline constexpr std::string_view kTriggerValueChanged = "trigger.ValueChanged";
inline constexpr std::string_view kTriggerSpawned = "trigger.Spawned";
inline constexpr std::string_view kTriggerDestroyed = "trigger.Destroyed";
inline constexpr std::string_view kTriggerOverlap = "trigger.Overlap";
inline constexpr std::string_view kTriggerOverlapEnd = "trigger.OverlapEnd";

inline constexpr std::string_view kFlowDelay = "flow.Delay";
inline constexpr std::string_view kFlowTimer = "flow.Timer";
inline constexpr std::string_view kFlowBranch = "flow.Branch";
inline constexpr std::string_view kFlowSequence = "flow.Sequence";
inline constexpr std::string_view kFlowRandom = "flow.Random";

inline constexpr std::string_view kWorldSetValue = "world.SetValue";
inline constexpr std::string_view kWorldModifyValue = "world.ModifyValue";
inline constexpr std::string_view kWorldDestroy = "world.Destroy";
inline constexpr std::string_view kWorldMove = "world.Move";
inline constexpr std::string_view kWorldRotate = "world.Rotate";
inline constexpr std::string_view kWorldShow = "world.Show";
inline constexpr std::string_view kWorldHide = "world.Hide";
inline constexpr std::string_view kWorldPlaySound = "world.PlaySound";
inline constexpr std::string_view kWorldSpawnPrefab = "world.SpawnPrefab";

/// flow.Timer fires at most this many times.
inline constexpr std::int32_t kMaxTimerCount = 1000;

/// The input a trigger node matches raised keys with.
inline constexpr std::string_view kTriggerKeyInput = "key";

/// The shared contract ids of the core library: add them to the host's engine contracts (ModuleHostDesc) or the
/// ModuleSet before registering the library.
[[nodiscard]] std::vector<std::string> CoreContracts();

/// Registers the core library as the engine (sdk::kEngineProviderId). The registry's ModuleSet must declare
/// CoreContracts().
Result<void> RegisterCoreLibrary(ChainRegistry& registry);

} // namespace interi::chain
