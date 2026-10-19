// SPDX-License-Identifier: MIT
#pragma once

// Chain Reaction events that drive animation. Shared contract ids the engine declares (AnimationContracts).
//
//   anim.Play     target, state, blend (s)   -> Out   fades the entity's Animator to the state
//   anim.Trigger  target, trigger            -> Out   takes the graph transition that trigger names
//
// Both only record a request on the entity's AnimatorComponent (applied at the end of the step, in activation order:
// the last request in a step wins); StepAnimator consumes it.

#include <interi/chain/Event.h>
#include <interi/core/Result.h>

#include <string>
#include <string_view>
#include <vector>

namespace interi::anim
{

inline constexpr std::string_view kAnimPlay = "anim.Play";
inline constexpr std::string_view kAnimTrigger = "anim.Trigger";

[[nodiscard]] std::vector<std::string> AnimationContracts();

/// Registers the events as the engine. The registry's ModuleSet must declare AnimationContracts().
Result<void> RegisterAnimationLibrary(chain::ChainRegistry& registry);

} // namespace interi::anim
