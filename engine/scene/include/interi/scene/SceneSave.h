// SPDX-License-Identifier: MIT
#pragma once

// A running scene as a save section ("scene.state", the engine's): the run-time Values, and for every object its
// local transform and whether it is hidden. Restoring goes into a freshly created runtime of the same scene (the
// way destruction state restores into a freshly loaded level):
//   - objects the save doesn't list (a collected coin, a chain-spawned crate that did not exist yet) are destroyed;
//   - objects the save lists but the scene no longer has are skipped and logged;
//   - run-time Values are replaced by the saved ones (Values no loaded module defines any more are dropped).
// Not saved: chain timers and pending continuations (they start over), the step counter, and anything a module
// keeps outside Values, which registers its own section (ModuleRegistrar::Saves()).

#include <interi/core/Result.h>
#include <interi/runtime/SaveGame.h>
#include <interi/scene/SceneRuntime.h>

#include <string_view>

namespace interi::scene
{

inline constexpr std::string_view kSceneSaveSection = "scene.state";
inline constexpr std::uint32_t kSceneSaveVersion = 1;

/// Registers the section for `runtime` with `registry` (as the engine). `runtime` must outlive the registration:
/// call `registry.RemoveProvider(kEngineSaveProvider)`-style cleanup (or destroy the registry) before the runtime.
Result<void> RegisterSceneSaveSection(SaveRegistry& registry, SceneRuntime& runtime);

/// The section's bytes, and applying them (what the registered functions call).
[[nodiscard]] std::vector<std::byte> CaptureSceneState(SceneRuntime& runtime);
Result<void> RestoreSceneState(SceneRuntime& runtime, std::span<const std::byte> data, std::uint32_t savedVersion);

} // namespace interi::scene
