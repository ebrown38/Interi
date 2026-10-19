// SPDX-License-Identifier: MIT
#pragma once

// Skeletal animation data and playback (M24). The importers of M21 are not required: a skeleton and a clip are small
// JSON assets (`.skeleton.json`, `.clip.json`) that an importer, a tool or a test can write.
//
//     skeleton: { "version": 1, "joints": [ { "name": "hip", "parent": -1, "position": [0, 1, 0],
//                                             "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }, ... ] }
//     clip:     { "version": 1, "name": "walk", "duration": 1.0,
//                 "tracks": [ { "joint": "hip", "position": [[0, x, y, z], ...],
//                               "rotation": [[0, x, y, z, w], ...], "scale": [[0, x, y, z], ...] } ] }
//
// Joints are listed parents first. Keys are [time, value...] with strictly increasing times inside [0, duration].
// Sampling interpolates positions and scales linearly and rotations with nlerp (no trigonometry: the result is
// bit-identical on every platform and thread count). Joints without a track keep their bind pose.

#include <interi/core/Result.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Transform.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::anim
{

inline constexpr std::size_t kMaxJoints = 256;

struct Joint
{
    std::string name;
    std::int32_t parent = -1;
    Transform bind; ///< Local bind pose.
    friend bool operator==(const Joint&, const Joint&) = default;
};

struct Skeleton
{
    std::vector<Joint> joints;

    /// The index of the joint called `name`, or -1.
    [[nodiscard]] std::int32_t FindJoint(std::string_view name) const noexcept;
    friend bool operator==(const Skeleton&, const Skeleton&) = default;
};

struct VectorKey
{
    float time = 0.0f;
    Vec3 value;
    friend bool operator==(const VectorKey&, const VectorKey&) = default;
};

struct RotationKey
{
    float time = 0.0f;
    Quat value;
    friend bool operator==(const RotationKey&, const RotationKey&) = default;
};

struct JointTrack
{
    std::int32_t joint = 0;
    std::vector<VectorKey> position;
    std::vector<RotationKey> rotation;
    std::vector<VectorKey> scale;
    friend bool operator==(const JointTrack&, const JointTrack&) = default;
};

struct Clip
{
    std::string name;
    float duration = 0.0f;
    std::vector<JointTrack> tracks;
    friend bool operator==(const Clip&, const Clip&) = default;
};

/// Local transforms, one per joint.
using Pose = std::vector<Transform>;

/// Parents-first, unique names, finite values, unit rotations (normalized when read), at most kMaxJoints.
Result<void> ValidateSkeleton(const Skeleton& skeleton);
/// The clip against `skeleton`: known joints, increasing key times inside the duration, finite values.
Result<void> ValidateClip(const Clip& clip, const Skeleton& skeleton);

Result<Skeleton> ParseSkeleton(std::string_view json);
[[nodiscard]] std::string WriteSkeleton(const Skeleton& skeleton);
Result<Clip> ParseClip(std::string_view json, const Skeleton& skeleton);
[[nodiscard]] std::string WriteClip(const Clip& clip, const Skeleton& skeleton);

[[nodiscard]] Pose BindPose(const Skeleton& skeleton);

/// Samples `clip` at `time` into `out` (resized to the skeleton). A looping clip wraps `time` into [0, duration); a
/// one-shot clip clamps it.
void SampleClip(const Skeleton& skeleton, const Clip& clip, float time, bool loop, Pose& out);

/// out = a * (1 - weight) + b * weight per joint (nlerp for rotations). `a` and `b` must be the same size.
void BlendPoses(const Pose& a, const Pose& b, float weight, Pose& out);

/// Model-space transform of every joint.
[[nodiscard]] std::vector<Mat4> ComputeModelPose(const Skeleton& skeleton, const Pose& pose);
/// Skinning matrices: model pose times the inverse bind model pose (identity at the bind pose).
[[nodiscard]] std::vector<Mat4> ComputeSkinMatrices(const Skeleton& skeleton, const Pose& pose);

} // namespace interi::anim
