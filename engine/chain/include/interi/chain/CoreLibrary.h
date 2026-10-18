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

#include <interi/chain/Event.h>
#include <interi/core/Result.h>

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

/// The input a trigger node matches raised keys with.
inline constexpr std::string_view kTriggerKeyInput = "key";

/// The shared contract ids of the core library: add them to the host's engine contracts (ModuleHostDesc) or the
/// ModuleSet before registering the library.
[[nodiscard]] std::vector<std::string> CoreContracts();

/// Registers the core library as the engine (sdk::kEngineProviderId). The registry's ModuleSet must declare
/// CoreContracts().
Result<void> RegisterCoreLibrary(ChainRegistry& registry);

} // namespace interi::chain
