// SPDX-License-Identifier: MIT
#pragma once

// A procedural two-joint arm and clips: "raise" lifts the elbow 2 m in 1 s, "lower" holds it still, so tests need no
// imported assets.

#include <interi/anim/Animator.h>

#include <gtest/gtest.h>

namespace interi::anim::testing
{

inline Skeleton ArmSkeleton()
{
    Skeleton skeleton;
    skeleton.joints.push_back({.name = "shoulder", .parent = -1, .bind = {}});
    Transform elbow;
    elbow.position = Vec3(0.0f, 1.0f, 0.0f);
    skeleton.joints.push_back({.name = "elbow", .parent = 0, .bind = elbow});
    return skeleton;
}

inline Clip RaiseClip()
{
    Clip clip;
    clip.name = "raise";
    clip.duration = 1.0f;
    clip.tracks.push_back(
        {.joint = 1,
         .position = {{.time = 0.0f, .value = Vec3(0.0f, 1.0f, 0.0f)}, {.time = 1.0f, .value = Vec3(0.0f, 3.0f, 0.0f)}},
         .rotation = {},
         .scale = {}});
    return clip;
}

inline Clip HoldClip(std::string name = "hold")
{
    Clip clip;
    clip.name = std::move(name);
    clip.duration = 1.0f;
    return clip;
}

inline AnimationSet ArmSet()
{
    AnimationSet set;
    set.id = "arm";
    set.skeleton = ArmSkeleton();
    set.clips = {RaiseClip(), HoldClip()};
    set.graph.defaultState = "idle";
    set.graph.states = {{.id = "idle", .clip = "hold", .loop = true, .speed = 1.0f},
                        {.id = "raise", .clip = "raise", .loop = false, .speed = 1.0f}};
    set.graph.transitions = {{.from = "idle", .to = "raise", .trigger = "go", .duration = 0.5f},
                             {.from = "*", .to = "idle", .trigger = "stop", .duration = 0.0f}};
    return set;
}

} // namespace interi::anim::testing
