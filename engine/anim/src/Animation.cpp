// SPDX-License-Identifier: MIT
#include <interi/anim/Animation.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace interi::anim
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool Finite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(const Quat& q) noexcept
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

float LengthSquared(const Quat& q) noexcept
{
    return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
}

template <typename Key>
std::size_t FindSegment(const std::vector<Key>& keys, float time) noexcept
{
    // The last key whose time <= `time`.
    const auto it = std::ranges::upper_bound(keys, time, {}, &Key::time);
    return static_cast<std::size_t>(it - keys.begin()) - 1;
}

Vec3 SampleVector(const std::vector<VectorKey>& keys, float time)
{
    if (time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }
    const std::size_t i = FindSegment(keys, time);
    const float span = keys[i + 1].time - keys[i].time;
    return Lerp(keys[i].value, keys[i + 1].value, (time - keys[i].time) / span);
}

Quat SampleRotation(const std::vector<RotationKey>& keys, float time)
{
    if (time <= keys.front().time)
    {
        return keys.front().value;
    }
    if (time >= keys.back().time)
    {
        return keys.back().value;
    }
    const std::size_t i = FindSegment(keys, time);
    const float span = keys[i + 1].time - keys[i].time;
    return Nlerp(keys[i].value, keys[i + 1].value, (time - keys[i].time) / span);
}

Result<Vec3> ReadVec3(const Json& value, std::string_view what, std::size_t offset = 0)
{
    if (!value.is_array() || value.size() != offset + 3)
    {
        return Invalid(std::format("{}: expected {} numbers", what, offset + 3));
    }
    for (std::size_t i = offset; i < offset + 3; ++i)
    {
        if (!value[i].is_number())
        {
            return Invalid(std::format("{}: expected numbers", what));
        }
    }
    return Vec3{value[offset].get<float>(), value[offset + 1].get<float>(), value[offset + 2].get<float>()};
}

Result<Quat> ReadQuat(const Json& value, std::string_view what, std::size_t offset = 0)
{
    if (!value.is_array() || value.size() != offset + 4)
    {
        return Invalid(std::format("{}: expected {} numbers", what, offset + 4));
    }
    for (std::size_t i = offset; i < offset + 4; ++i)
    {
        if (!value[i].is_number())
        {
            return Invalid(std::format("{}: expected numbers", what));
        }
    }
    const Quat q{value[offset].get<float>(), value[offset + 1].get<float>(), value[offset + 2].get<float>(),
                 value[offset + 3].get<float>()};
    if (!Finite(q) || LengthSquared(q) < 1e-12f)
    {
        return Invalid(std::format("{}: a rotation must be finite and non-zero", what));
    }
    return Normalize(q);
}

Json ToJson(const Vec3& v)
{
    return Json::array({v.x, v.y, v.z});
}

Json ToJson(const Quat& q)
{
    return Json::array({q.x, q.y, q.z, q.w});
}

Result<Json> ParseDocument(std::string_view text, std::string_view kind)
{
    Json document = Json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object())
    {
        return Invalid(std::format("{}: not a JSON object", kind));
    }
    const auto version = document.find("version");
    if (version == document.end() || !version->is_number_integer())
    {
        return Invalid(std::format("{}: missing \"version\"", kind));
    }
    if (version->get<int>() != 1)
    {
        return MakeError(ErrorCode::VersionMismatch, std::format("{}: unsupported version", kind));
    }
    return document;
}

template <typename Key, typename Read>
Result<std::vector<Key>> ReadKeys(const Json& track, const char* field, std::size_t width, Read read)
{
    std::vector<Key> keys;
    const auto it = track.find(field);
    if (it == track.end())
    {
        return keys;
    }
    if (!it->is_array())
    {
        return Invalid(std::format("\"{}\" must be an array of keys", field));
    }
    for (const Json& entry : *it)
    {
        if (!entry.is_array() || entry.size() != width + 1 || !entry[0].is_number())
        {
            return Invalid(std::format("\"{}\": a key is [time, {} numbers]", field, width));
        }
        auto value = read(entry, field, 1);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        keys.push_back({.time = entry[0].get<float>(), .value = *value});
    }
    return keys;
}

} // namespace

std::int32_t Skeleton::FindJoint(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < joints.size(); ++i)
    {
        if (joints[i].name == name)
        {
            return static_cast<std::int32_t>(i);
        }
    }
    return -1;
}

Result<void> ValidateSkeleton(const Skeleton& skeleton)
{
    if (skeleton.joints.empty() || skeleton.joints.size() > kMaxJoints)
    {
        return Invalid(std::format("a skeleton has 1 to {} joints", kMaxJoints));
    }
    std::set<std::string_view> names;
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
    {
        const Joint& joint = skeleton.joints[i];
        if (joint.name.empty() || !names.insert(joint.name).second)
        {
            return Invalid(std::format("joint {}: empty or duplicate name '{}'", i, joint.name));
        }
        if (joint.parent < -1 || joint.parent >= static_cast<std::int32_t>(i))
        {
            return Invalid(
                std::format("joint '{}': parent {} must be -1 or an earlier joint", joint.name, joint.parent));
        }
        if (!Finite(joint.bind.position) || !Finite(joint.bind.scale) || !Finite(joint.bind.rotation))
        {
            return Invalid(std::format("joint '{}': non-finite bind pose", joint.name));
        }
    }
    return {};
}

Result<void> ValidateClip(const Clip& clip, const Skeleton& skeleton)
{
    if (!std::isfinite(clip.duration) || clip.duration <= 0.0f)
    {
        return Invalid(std::format("clip '{}': duration must be positive", clip.name));
    }
    std::set<std::int32_t> seen;
    const auto check = [&](const auto& keys, std::string_view what) -> Result<void>
    {
        float previous = -1.0f;
        for (const auto& key : keys)
        {
            if (!std::isfinite(key.time) || key.time < 0.0f || key.time > clip.duration || key.time <= previous)
            {
                return Invalid(
                    std::format("clip '{}': {} key times must increase inside [0, duration]", clip.name, what));
            }
            previous = key.time;
        }
        return {};
    };
    for (const JointTrack& track : clip.tracks)
    {
        if (track.joint < 0 || track.joint >= static_cast<std::int32_t>(skeleton.joints.size()) ||
            !seen.insert(track.joint).second)
        {
            return Invalid(std::format("clip '{}': unknown or repeated joint {}", clip.name, track.joint));
        }
        INTERI_TRY(check(track.position, "position"));
        INTERI_TRY(check(track.rotation, "rotation"));
        INTERI_TRY(check(track.scale, "scale"));
    }
    return {};
}

Result<Skeleton> ParseSkeleton(std::string_view text)
{
    auto document = ParseDocument(text, "skeleton");
    if (!document)
    {
        return std::unexpected(document.error());
    }
    const auto joints = document->find("joints");
    if (joints == document->end() || !joints->is_array())
    {
        return Invalid("skeleton: missing \"joints\"");
    }
    Skeleton skeleton;
    for (const Json& entry : *joints)
    {
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string())
        {
            return Invalid("skeleton: a joint needs a \"name\"");
        }
        Joint joint;
        joint.name = entry["name"].get<std::string>();
        if (entry.contains("parent"))
        {
            if (!entry["parent"].is_number_integer())
            {
                return Invalid(std::format("joint '{}': \"parent\" must be an integer", joint.name));
            }
            joint.parent = entry["parent"].get<std::int32_t>();
        }
        if (entry.contains("position"))
        {
            auto v = ReadVec3(entry["position"], joint.name);
            if (!v)
            {
                return std::unexpected(v.error());
            }
            joint.bind.position = *v;
        }
        if (entry.contains("rotation"))
        {
            auto q = ReadQuat(entry["rotation"], joint.name);
            if (!q)
            {
                return std::unexpected(q.error());
            }
            joint.bind.rotation = *q;
        }
        if (entry.contains("scale"))
        {
            auto v = ReadVec3(entry["scale"], joint.name);
            if (!v)
            {
                return std::unexpected(v.error());
            }
            joint.bind.scale = *v;
        }
        skeleton.joints.push_back(std::move(joint));
    }
    INTERI_TRY(ValidateSkeleton(skeleton));
    return skeleton;
}

std::string WriteSkeleton(const Skeleton& skeleton)
{
    Json joints = Json::array();
    for (const Joint& joint : skeleton.joints)
    {
        joints.push_back({{"name", joint.name},
                          {"parent", joint.parent},
                          {"position", ToJson(joint.bind.position)},
                          {"rotation", ToJson(joint.bind.rotation)},
                          {"scale", ToJson(joint.bind.scale)}});
    }
    return Json{{"version", 1}, {"joints", std::move(joints)}}.dump(2) + "\n";
}

Result<Clip> ParseClip(std::string_view text, const Skeleton& skeleton)
{
    auto document = ParseDocument(text, "clip");
    if (!document)
    {
        return std::unexpected(document.error());
    }
    Clip clip;
    if (document->contains("name") && (*document)["name"].is_string())
    {
        clip.name = (*document)["name"].get<std::string>();
    }
    if (!document->contains("duration") || !(*document)["duration"].is_number())
    {
        return Invalid("clip: missing \"duration\"");
    }
    clip.duration = (*document)["duration"].get<float>();
    const auto tracks = document->find("tracks");
    if (tracks != document->end())
    {
        if (!tracks->is_array())
        {
            return Invalid("clip: \"tracks\" must be an array");
        }
        for (const Json& entry : *tracks)
        {
            if (!entry.is_object() || !entry.contains("joint") || !entry["joint"].is_string())
            {
                return Invalid("clip: a track needs a \"joint\" name");
            }
            JointTrack track;
            track.joint = skeleton.FindJoint(entry["joint"].get<std::string>());
            if (track.joint < 0)
            {
                return Invalid(std::format("clip: unknown joint '{}'", entry["joint"].get<std::string>()));
            }
            auto position =
                ReadKeys<VectorKey>(entry, "position", 3,
                                    [](const Json& j, std::string_view w, std::size_t o) { return ReadVec3(j, w, o); });
            if (!position)
            {
                return std::unexpected(position.error());
            }
            auto rotation =
                ReadKeys<RotationKey>(entry, "rotation", 4, [](const Json& j, std::string_view w, std::size_t o)
                                      { return ReadQuat(j, w, o); });
            if (!rotation)
            {
                return std::unexpected(rotation.error());
            }
            auto scale = ReadKeys<VectorKey>(entry, "scale", 3, [](const Json& j, std::string_view w, std::size_t o)
                                             { return ReadVec3(j, w, o); });
            if (!scale)
            {
                return std::unexpected(scale.error());
            }
            track.position = std::move(*position);
            track.rotation = std::move(*rotation);
            track.scale = std::move(*scale);
            clip.tracks.push_back(std::move(track));
        }
    }
    INTERI_TRY(ValidateClip(clip, skeleton));
    return clip;
}

std::string WriteClip(const Clip& clip, const Skeleton& skeleton)
{
    Json tracks = Json::array();
    for (const JointTrack& track : clip.tracks)
    {
        Json entry{{"joint", skeleton.joints[static_cast<std::size_t>(track.joint)].name}};
        const auto vectors = [](const std::vector<VectorKey>& keys)
        {
            Json out = Json::array();
            for (const VectorKey& key : keys)
            {
                out.push_back({key.time, key.value.x, key.value.y, key.value.z});
            }
            return out;
        };
        if (!track.position.empty())
        {
            entry["position"] = vectors(track.position);
        }
        if (!track.rotation.empty())
        {
            Json out = Json::array();
            for (const RotationKey& key : track.rotation)
            {
                out.push_back({key.time, key.value.x, key.value.y, key.value.z, key.value.w});
            }
            entry["rotation"] = std::move(out);
        }
        if (!track.scale.empty())
        {
            entry["scale"] = vectors(track.scale);
        }
        tracks.push_back(std::move(entry));
    }
    return Json{{"version", 1}, {"name", clip.name}, {"duration", clip.duration}, {"tracks", std::move(tracks)}}.dump(
               2) +
           "\n";
}

Pose BindPose(const Skeleton& skeleton)
{
    Pose pose;
    pose.reserve(skeleton.joints.size());
    for (const Joint& joint : skeleton.joints)
    {
        pose.push_back(joint.bind);
    }
    return pose;
}

void SampleClip(const Skeleton& skeleton, const Clip& clip, float time, bool loop, Pose& out)
{
    out = BindPose(skeleton);
    if (clip.duration > 0.0f)
    {
        if (loop)
        {
            time -= std::floor(time / clip.duration) * clip.duration;
            time = std::clamp(time, 0.0f, clip.duration);
        }
        else
        {
            time = std::clamp(time, 0.0f, clip.duration);
        }
    }
    for (const JointTrack& track : clip.tracks)
    {
        Transform& local = out[static_cast<std::size_t>(track.joint)];
        if (!track.position.empty())
        {
            local.position = SampleVector(track.position, time);
        }
        if (!track.rotation.empty())
        {
            local.rotation = SampleRotation(track.rotation, time);
        }
        if (!track.scale.empty())
        {
            local.scale = SampleVector(track.scale, time);
        }
    }
}

void BlendPoses(const Pose& a, const Pose& b, float weight, Pose& out)
{
    out.resize(a.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        out[i].position = Lerp(a[i].position, b[i].position, weight);
        out[i].rotation = Nlerp(a[i].rotation, b[i].rotation, weight);
        out[i].scale = Lerp(a[i].scale, b[i].scale, weight);
    }
}

std::vector<Mat4> ComputeModelPose(const Skeleton& skeleton, const Pose& pose)
{
    std::vector<Mat4> model(skeleton.joints.size());
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
    {
        const Mat4 local = pose[i].ToMat4();
        const std::int32_t parent = skeleton.joints[i].parent;
        model[i] = parent < 0 ? local : model[static_cast<std::size_t>(parent)] * local;
    }
    return model;
}

std::vector<Mat4> ComputeSkinMatrices(const Skeleton& skeleton, const Pose& pose)
{
    std::vector<Mat4> model = ComputeModelPose(skeleton, pose);
    const std::vector<Mat4> bind = ComputeModelPose(skeleton, BindPose(skeleton));
    for (std::size_t i = 0; i < model.size(); ++i)
    {
        model[i] = model[i] * Inverse(bind[i]);
    }
    return model;
}

} // namespace interi::anim
