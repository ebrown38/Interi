// SPDX-License-Identifier: MIT
#include <interi/assets/Skeleton.h>

#include "AssetIo.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace interi::assets
{

namespace
{

constexpr FourCC kSkeletonPayload = MakeFourCC("ISKP");
constexpr FourCC kClipPayload = MakeFourCC("IANP");
constexpr FourCC kSkinPayload = MakeFourCC("ISNP");

std::unexpected<Error> Corrupt(std::string message)
{
    return MakeError(ErrorCode::Corrupt, std::move(message));
}

bool Finite(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(const Vec4& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

bool Finite(const Quat& q)
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

} // namespace

template <typename Archive>
void Serialize(Archive& archive, JointTransform& t)
{
    archive(t.translation, t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w, t.scale);
}

template <typename Archive>
void Serialize(Archive& archive, Joint& joint)
{
    archive(joint.name, joint.parent, joint.bind);
    for (Vec4& column : joint.inverseBind.columns)
    {
        archive(column);
    }
}

template <typename Archive>
void Serialize(Archive& archive, Skeleton& skeleton)
{
    archive(skeleton.name, skeleton.joints);
}

template <typename Archive>
void Serialize(Archive& archive, AnimationChannel& channel)
{
    archive(channel.joint, channel.path, channel.interpolation, channel.times, channel.values);
}

template <typename Archive>
void Serialize(Archive& archive, AnimationClip& clip)
{
    archive(clip.name, clip.duration, clip.channels);
}

template <typename Archive>
void Serialize(Archive& archive, SkinInfluence& influence)
{
    for (std::size_t i = 0; i < kMaxSkinInfluences; ++i)
    {
        archive(influence.joints[i], influence.weights[i]);
    }
}

template <typename Archive>
void Serialize(Archive& archive, SkinBinding& skin)
{
    archive(skin.skeleton, skin.influences);
}

Result<void> ValidateSkeleton(const Skeleton& skeleton)
{
    if (skeleton.joints.size() > kMaxJoints)
    {
        return Corrupt(std::format("skeleton: {} joints (at most {})", skeleton.joints.size(), kMaxJoints));
    }
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
    {
        const Joint& joint = skeleton.joints[i];
        if (joint.parent < -1 || (joint.parent >= 0 && static_cast<std::size_t>(joint.parent) >= i))
        {
            return Corrupt(
                std::format("skeleton: joint {} has parent {}, which is not an earlier joint", i, joint.parent));
        }
        const bool finite = Finite(joint.bind.translation) && Finite(joint.bind.rotation) && Finite(joint.bind.scale) &&
                            std::ranges::all_of(joint.inverseBind.columns, [](const Vec4& c) { return Finite(c); });
        if (!finite)
        {
            return Corrupt(std::format("skeleton: joint {} has a non-finite transform", i));
        }
    }
    return {};
}

Result<void> ValidateClip(const AnimationClip& clip, std::uint32_t jointCount)
{
    if (!std::isfinite(clip.duration) || clip.duration < 0.0f)
    {
        return Corrupt("clip: bad duration");
    }
    for (std::size_t c = 0; c < clip.channels.size(); ++c)
    {
        const AnimationChannel& channel = clip.channels[c];
        if (channel.joint >= jointCount)
        {
            return Corrupt(std::format("clip: channel {} animates joint {} of {}", c, channel.joint, jointCount));
        }
        if (static_cast<std::uint8_t>(channel.path) > 2 || static_cast<std::uint8_t>(channel.interpolation) > 1)
        {
            return Corrupt(std::format("clip: channel {} has an unknown path or interpolation", c));
        }
        if (channel.times.empty() || channel.times.size() != channel.values.size())
        {
            return Corrupt(std::format("clip: channel {} needs one value per time", c));
        }
        float previous = -1.0f;
        for (std::size_t k = 0; k < channel.times.size(); ++k)
        {
            const float time = channel.times[k];
            if (!std::isfinite(time) || time < 0.0f || time <= previous || time > clip.duration ||
                !Finite(channel.values[k]))
            {
                return Corrupt(
                    std::format("clip: channel {} key {} is out of order, out of range or not finite", c, k));
            }
            previous = time;
            if (channel.path == ChannelPath::Rotation)
            {
                const Vec4& q = channel.values[k];
                if (std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) > 1e-3f)
                {
                    return Corrupt(std::format("clip: channel {} key {} is not a unit rotation", c, k));
                }
            }
        }
    }
    return {};
}

Result<void> ValidateSkin(const SkinBinding& skin, std::uint32_t jointCount)
{
    for (std::size_t i = 0; i < skin.influences.size(); ++i)
    {
        const SkinInfluence& influence = skin.influences[i];
        float total = 0.0f;
        for (std::size_t k = 0; k < kMaxSkinInfluences; ++k)
        {
            const float weight = influence.weights[k];
            if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f ||
                (weight > 0.0f && influence.joints[k] >= jointCount))
            {
                return Corrupt(std::format("skin: corner {} has a bad weight or joint", i));
            }
            total += weight;
        }
        if (total != 0.0f && std::abs(total - 1.0f) > 1e-3f)
        {
            return Corrupt(std::format("skin: corner {} weights add up to {}", i, total));
        }
    }
    return {};
}

std::vector<std::byte> WriteSkeletonAsset(const Skeleton& skeleton)
{
    return detail::WriteHashed(kSkeletonMagic, kSkeletonPayload, kSkeletalVersion, skeleton);
}

Result<Skeleton> ReadSkeletonAsset(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(
        skeleton, detail::ReadHashed<Skeleton>(data, kSkeletonMagic, kSkeletonPayload, kSkeletalVersion, "skeleton"));
    INTERI_TRY(ValidateSkeleton(skeleton));
    return skeleton;
}

std::vector<std::byte> WriteClipAsset(const AnimationClip& clip)
{
    return detail::WriteHashed(kClipMagic, kClipPayload, kSkeletalVersion, clip);
}

Result<AnimationClip> ReadClipAsset(std::span<const std::byte> data, std::uint32_t jointCount)
{
    INTERI_TRY_ASSIGN(clip,
                      detail::ReadHashed<AnimationClip>(data, kClipMagic, kClipPayload, kSkeletalVersion, "clip"));
    INTERI_TRY(ValidateClip(clip, jointCount));
    return clip;
}

std::vector<std::byte> WriteSkinAsset(const SkinBinding& skin)
{
    return detail::WriteHashed(kSkinMagic, kSkinPayload, kSkeletalVersion, skin);
}

Result<SkinBinding> ReadSkinAsset(std::span<const std::byte> data, std::uint32_t jointCount)
{
    INTERI_TRY_ASSIGN(skin, detail::ReadHashed<SkinBinding>(data, kSkinMagic, kSkinPayload, kSkeletalVersion, "skin"));
    INTERI_TRY(ValidateSkin(skin, jointCount));
    return skin;
}

} // namespace interi::assets
