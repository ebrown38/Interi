// SPDX-License-Identifier: MIT
#pragma once

// Animation states (M24): a state graph per animation set, played by an AnimatorComponent on an entity and driven by
// chains (anim.Play / anim.Trigger write requests; StepAnimator consumes them on the fixed step).
//
//     { "version": 1, "default": "idle",
//       "states": [ { "id": "idle", "clip": "idle", "loop": true, "speed": 1.0 },
//                   { "id": "run",  "clip": "run" },
//                   { "id": "jump", "clip": "jump", "loop": false } ],
//       "transitions": [ { "from": "idle", "to": "run", "trigger": "move", "duration": 0.2 },
//                        { "from": "*", "to": "jump", "trigger": "jump", "duration": 0.1 } ] }
//
// An animation set is a skeleton, its clips and a graph. Sets are bound to character archetypes (the values module's
// archetype ids), so every entity of an archetype shares one graph. Everything here is pure and deterministic.

#include <interi/anim/Animation.h>
#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>
#include <interi/sdk/Manifest.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace interi::anim
{

struct AnimState
{
    std::string id;
    std::string clip;
    bool loop = true;
    float speed = 1.0f;
    friend bool operator==(const AnimState&, const AnimState&) = default;
};

struct AnimTransition
{
    std::string from; ///< A state id, or "*" for any.
    std::string to;
    std::string trigger;
    float duration = 0.2f; ///< Crossfade seconds.
    friend bool operator==(const AnimTransition&, const AnimTransition&) = default;
};

struct AnimGraph
{
    std::string defaultState;
    std::vector<AnimState> states;
    std::vector<AnimTransition> transitions;
    friend bool operator==(const AnimGraph&, const AnimGraph&) = default;
};

Result<AnimGraph> ParseAnimGraph(std::string_view json);
[[nodiscard]] std::string WriteAnimGraph(const AnimGraph& graph);

struct AnimationSet
{
    std::string id;
    Skeleton skeleton;
    std::vector<Clip> clips;
    AnimGraph graph;
};

/// Name hash used for state and trigger ids inside AnimatorComponent (FNV-1a; 0 is reserved for "none").
[[nodiscard]] std::uint32_t AnimNameHash(std::string_view name) noexcept;

/// The runtime state of one animated entity. Trivially copyable.
struct AnimatorComponent
{
    static constexpr const char* kComponentName = "interi.Animator";

    std::uint32_t set = 0;   ///< Index in the AnimationLibrary.
    std::uint32_t state = 0; ///< AnimNameHash of the playing state (0: the default).
    std::uint32_t next = 0;  ///< The state being faded to (0: none).
    float time = 0.0f;       ///< Seconds into the playing clip.
    float nextTime = 0.0f;
    float blendDuration = 0.0f;
    float blendElapsed = 0.0f;
    std::uint32_t requestState = 0; ///< Set by anim.Play, consumed by the next StepAnimator.
    float requestBlend = 0.0f;
    std::uint32_t requestTrigger = 0; ///< Set by anim.Trigger.
};

/// The sets of a project, and which archetype uses which.
class AnimationLibrary
{
public:
    /// InvalidArgument for an invalid set (skeleton, clips, graph: unknown clips, states or default; duplicate ids) or
    /// a repeated set id.
    Result<std::uint32_t> Add(AnimationSet set);
    /// Archetype `archetype` uses set `setId` (NotFound if the set is unknown).
    Result<void> BindArchetype(std::string_view archetype, std::string_view setId);

    [[nodiscard]] std::size_t Size() const noexcept { return m_sets.size(); }
    [[nodiscard]] const AnimationSet& Get(std::uint32_t index) const { return m_sets[index]; }
    [[nodiscard]] const AnimationSet* Find(std::string_view setId) const noexcept;
    /// A fresh animator for an entity of `archetype`; NotFound when the archetype has no set.
    [[nodiscard]] Result<AnimatorComponent> MakeAnimator(std::string_view archetype) const;

private:
    std::vector<AnimationSet> m_sets;
    std::map<std::string, std::uint32_t, std::less<>> m_archetypes;
};

/// Loads the manifest's `animationSets` (files read from `moduleFiles`, rooted at the module's folder) into `library`
/// as sets `<module id>:<name>` and binds their archetypes. All or nothing per module: on an error the library may
/// hold the sets loaded before it. NotFound/IoError for a missing file, InvalidArgument for invalid data.
Result<void> LoadModuleAnimations(const sdk::ModuleManifest& manifest, const IFileSystem& moduleFiles,
                                  AnimationLibrary& library);

/// Applies the animator's requests, advances it by `dt` seconds and writes the resulting local pose.
/// anim.Trigger takes the first transition (in graph order) from the playing state or "*" whose trigger matches;
/// anim.Play fades to a state directly. An unknown request is ignored. A request while fading snaps the fade to its
/// target first. One-shot states stay on their last frame.
void StepAnimator(const AnimationLibrary& library, AnimatorComponent& animator, float dt, Pose& out);

} // namespace interi::anim
