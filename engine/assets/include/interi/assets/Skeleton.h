// SPDX-License-Identifier: MIT
#pragma once

// Skeletal data imported from models: a joint hierarchy (.iskel), animation clips (.ianim) and the skin of one mesh
// (.iskin: per-corner joint influences). Playback and blending come with the animation runtime; this is the data
// and its validation. Each file is magic, version, payload hash, payload archive (the same container as .imesh).

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/FileSystem.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::assets
{

inline constexpr std::uint32_t kMaxJoints = 1024;
inline constexpr std::uint32_t kMaxSkinInfluences = 4;

/// A joint's local transform: scale, then rotation, then translation.
struct JointTransform
{
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};

    friend bool operator==(const JointTransform&, const JointTransform&) = default;
};

struct Joint
{
    std::string name;
    std::int32_t parent = -1; ///< Index of an earlier joint, or -1 for a root.
    JointTransform bind;      ///< Local bind pose.
    Mat4 inverseBind = Mat4::Identity();

    friend bool operator==(const Joint&, const Joint&) = default;
};

struct Skeleton
{
    std::string name;
    std::vector<Joint> joints; ///< Parents come before their children.

    friend bool operator==(const Skeleton&, const Skeleton&) = default;
};

/// At most kMaxJoints, every parent an earlier joint, finite transforms. Corrupt with the reason.
[[nodiscard]] Result<void> ValidateSkeleton(const Skeleton& skeleton);

enum class ChannelPath : std::uint8_t
{
    Translation,
    Rotation,
    Scale,
};
enum class Interpolation : std::uint8_t
{
    Step,
    Linear, ///< Rotations interpolate along the shortest arc.
};

/// Keys of one joint property. Values are (x, y, z, unused) for translation and scale, (x, y, z, w) for rotation.
struct AnimationChannel
{
    std::uint32_t joint = 0;
    ChannelPath path = ChannelPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times; ///< Seconds, increasing.
    std::vector<Vec4> values; ///< One per time.

    friend bool operator==(const AnimationChannel&, const AnimationChannel&) = default;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f; ///< At least the last key time.
    std::vector<AnimationChannel> channels;

    friend bool operator==(const AnimationClip&, const AnimationClip&) = default;
};

/// Times finite, not negative and strictly increasing, one value per time, finite values, unit rotations (within
/// 1e-3), joints below `jointCount`, duration covering the keys.
[[nodiscard]] Result<void> ValidateClip(const AnimationClip& clip, std::uint32_t jointCount);

/// Four joint influences of one mesh corner; unused slots have weight 0. Weights add up to 1 (or all are 0).
struct SkinInfluence
{
    std::array<std::uint16_t, kMaxSkinInfluences> joints{};
    std::array<float, kMaxSkinInfluences> weights{};

    friend bool operator==(const SkinInfluence&, const SkinInfluence&) = default;
};

/// The skin of a mesh: the skeleton it binds to and one SkinInfluence per mesh corner (index).
struct SkinBinding
{
    std::string skeleton; ///< Virtual path of the `.iskel`.
    std::vector<SkinInfluence> influences;

    friend bool operator==(const SkinBinding&, const SkinBinding&) = default;
};

/// Joints below `jointCount`, finite weights in [0, 1] adding up to 1 within 1e-3 (or all zero).
[[nodiscard]] Result<void> ValidateSkin(const SkinBinding& skin, std::uint32_t jointCount);

inline constexpr FourCC kSkeletonMagic = MakeFourCC("ISKL");
inline constexpr FourCC kClipMagic = MakeFourCC("IANM");
inline constexpr FourCC kSkinMagic = MakeFourCC("ISKN");
inline constexpr std::uint32_t kSkeletalVersion = 1; ///< All three files.

[[nodiscard]] std::vector<std::byte> WriteSkeletonAsset(const Skeleton& skeleton);
[[nodiscard]] Result<Skeleton> ReadSkeletonAsset(std::span<const std::byte> data);
[[nodiscard]] std::vector<std::byte> WriteClipAsset(const AnimationClip& clip);
/// The jointCount of the skeleton the clip animates, to validate the joint indices against.
[[nodiscard]] Result<AnimationClip> ReadClipAsset(std::span<const std::byte> data, std::uint32_t jointCount);
[[nodiscard]] std::vector<std::byte> WriteSkinAsset(const SkinBinding& skin);
[[nodiscard]] Result<SkinBinding> ReadSkinAsset(std::span<const std::byte> data, std::uint32_t jointCount);

} // namespace interi::assets
