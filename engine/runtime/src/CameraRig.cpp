// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/runtime/CameraRig.h>
#include <interi/runtime/FollowCamera.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace interi
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

Result<float> ReadFloat(const Json& value, std::string_view rig, std::string_view field)
{
    if (!value.is_number() || !std::isfinite(value.get<double>()))
    {
        return Invalid(std::format("rig '{}': \"{}\" must be a finite number", rig, field));
    }
    return value.get<float>();
}

Result<Vec3> ReadVec3(const Json& value, std::string_view rig, std::string_view field)
{
    if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number())
    {
        return Invalid(std::format("rig '{}': \"{}\" must be [x, y, z]", rig, field));
    }
    return Vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
}

Vec3 ForwardOf(float yaw, float pitch) noexcept
{
    const float cosPitch = std::cos(pitch);
    return {-std::sin(yaw) * cosPitch, std::sin(pitch), -std::cos(yaw) * cosPitch};
}

} // namespace

Result<CameraRigSet> ParseCameraRigs(std::string_view json)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("camera rigs: not a JSON object");
    }
    CameraRigSet set;
    for (const auto& [key, value] : root.items())
    {
        if (key != "rigs")
        {
            set.warnings.push_back(std::format("camera rigs: unknown field \"{}\" ignored", key));
            continue;
        }
        if (!value.is_array())
        {
            return Invalid("camera rigs: \"rigs\" must be an array");
        }
        for (const Json& entry : value)
        {
            if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string())
            {
                return Invalid("camera rigs: every rig needs a \"name\"");
            }
            CameraRig rig;
            rig.name = entry["name"].get<std::string>();
            if (std::ranges::any_of(set.rigs, [&](const CameraRig& other) { return other.name == rig.name; }))
            {
                return Invalid(std::format("camera rigs: duplicate rig '{}'", rig.name));
            }
            const auto kind = entry.find("kind");
            if (kind == entry.end() || !kind->is_string())
            {
                return Invalid(std::format("rig '{}': needs a \"kind\"", rig.name));
            }
            const std::string kindName = kind->get<std::string>();
            if (kindName == "orbit")
            {
                rig.kind = CameraRigKind::Orbit;
            }
            else if (kindName == "firstPerson")
            {
                rig.kind = CameraRigKind::FirstPerson;
            }
            else if (kindName == "fixed")
            {
                rig.kind = CameraRigKind::Fixed;
            }
            else if (kindName == "topDown")
            {
                rig.kind = CameraRigKind::TopDown;
            }
            else
            {
                return Invalid(std::format("rig '{}': unknown kind \"{}\"", rig.name, kindName));
            }
            for (const auto& [field, fieldValue] : entry.items())
            {
                float* target = nullptr;
                if (field == "name" || field == "kind")
                {
                    continue;
                }
                if (field == "fov")
                {
                    target = &rig.fov;
                }
                else if (field == "distance")
                {
                    target = &rig.distance;
                }
                else if (field == "shoulder")
                {
                    target = &rig.shoulder;
                }
                else if (field == "height")
                {
                    target = &rig.height;
                }
                else if (field == "pitch")
                {
                    target = &rig.pitch;
                }
                else if (field == "eyeHeight")
                {
                    target = &rig.eyeHeight;
                }
                else if (field == "minPitch")
                {
                    target = &rig.minPitch;
                }
                else if (field == "maxPitch")
                {
                    target = &rig.maxPitch;
                }
                else if (field == "sensitivity")
                {
                    target = &rig.sensitivity;
                }
                if (target != nullptr)
                {
                    INTERI_TRY_ASSIGN(number, ReadFloat(fieldValue, rig.name, field));
                    *target = number;
                }
                else if (field == "position")
                {
                    INTERI_TRY_ASSIGN(position, ReadVec3(fieldValue, rig.name, field));
                    rig.position = position;
                }
                else if (field == "lookAt")
                {
                    INTERI_TRY_ASSIGN(lookAt, ReadVec3(fieldValue, rig.name, field));
                    rig.lookAt = lookAt;
                }
                else
                {
                    set.warnings.push_back(std::format("rig '{}': unknown field \"{}\" ignored", rig.name, field));
                }
            }
            if (rig.fov <= 1.0f || rig.fov >= 170.0f)
            {
                return Invalid(std::format("rig '{}': \"fov\" must be between 1 and 170 degrees", rig.name));
            }
            if (rig.distance < 0.0f || rig.height < 0.0f)
            {
                return Invalid(std::format(R"(rig '{}': "distance" and "height" must not be negative)", rig.name));
            }
            if (rig.kind == CameraRigKind::TopDown && (rig.pitch < -1.5f || rig.pitch >= 0.0f))
            {
                return Invalid(std::format("rig '{}': a topDown \"pitch\" must be in [-1.5, 0)", rig.name));
            }
            if (rig.kind == CameraRigKind::Fixed && LengthSquared(rig.lookAt - rig.position) < 1e-8f)
            {
                return Invalid(std::format(R"(rig '{}': "position" and "lookAt" coincide)", rig.name));
            }
            if (rig.minPitch > rig.maxPitch)
            {
                return Invalid(std::format(R"(rig '{}': "minPitch" is above "maxPitch")", rig.name));
            }
            set.rigs.push_back(std::move(rig));
        }
    }
    for (const std::string& warning : set.warnings)
    {
        INTERI_LOG_WARN("Camera", "{}", warning);
    }
    return set;
}

CameraPose EvaluateRig(const CameraRig& rig, float yaw, float pitch, const Vec3& target) noexcept
{
    CameraPose pose;
    pose.fov = rig.fov;
    switch (rig.kind)
    {
    case CameraRigKind::Orbit:
    {
        FollowCamera follow;
        follow.yaw = yaw;
        follow.pitch = std::clamp(pitch, rig.minPitch, rig.maxPitch);
        follow.distance = rig.distance;
        follow.shoulder = rig.shoulder;
        pose.eye = follow.Eye(target);
        pose.forward = follow.Forward();
        break;
    }
    case CameraRigKind::FirstPerson:
        pose.eye = target + Vec3(0.0f, rig.eyeHeight, 0.0f);
        pose.forward = ForwardOf(yaw, std::clamp(pitch, rig.minPitch, rig.maxPitch));
        break;
    case CameraRigKind::Fixed:
        pose.eye = rig.position;
        pose.forward = NormalizeOrZero(rig.lookAt - rig.position);
        break;
    case CameraRigKind::TopDown:
        pose.forward = ForwardOf(yaw, rig.pitch);
        pose.eye = target - pose.forward * (rig.height / -std::sin(rig.pitch));
        break;
    }
    return pose;
}

CameraController::CameraController(std::vector<CameraRig> rigs, float blendSeconds)
    : m_rigs(std::move(rigs))
    , m_blendSeconds(std::max(blendSeconds, 0.0f))
{
}

bool CameraController::SwitchTo(std::string_view name)
{
    const auto found = std::ranges::find(m_rigs, name, &CameraRig::name);
    if (found == m_rigs.end())
    {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - m_rigs.begin());
    if (index == m_active)
    {
        return true;
    }
    m_events.push_back({.from = m_rigs[m_active].name, .to = found->name});
    m_previous = m_active;
    m_active = index;
    m_blend = m_blendSeconds > 0.0f ? 0.0f : 1.0f;
    return true;
}

void CameraController::SwitchNext()
{
    if (!m_rigs.empty())
    {
        SwitchTo(m_rigs[(m_active + 1) % m_rigs.size()].name);
    }
}

void CameraController::Rotate(float deltaX, float deltaY) noexcept
{
    const CameraRig* rig = Active();
    if (rig == nullptr)
    {
        return;
    }
    m_yaw -= deltaX * rig->sensitivity;
    m_pitch = std::clamp(m_pitch - deltaY * rig->sensitivity, rig->minPitch, rig->maxPitch);
}

void CameraController::SetView(float yaw, float pitch) noexcept
{
    m_yaw = yaw;
    m_pitch = pitch;
}

void CameraController::Update(float dt) noexcept
{
    if (m_blend < 1.0f)
    {
        m_blend = m_blendSeconds > 0.0f ? std::min(1.0f, m_blend + dt / m_blendSeconds) : 1.0f;
    }
}

CameraPose CameraController::Pose(const Vec3& target) const noexcept
{
    if (m_rigs.empty())
    {
        return {};
    }
    const CameraPose now = EvaluateRig(m_rigs[m_active], m_yaw, m_pitch, target);
    if (m_blend >= 1.0f)
    {
        return now;
    }
    const CameraPose before = EvaluateRig(m_rigs[m_previous], m_yaw, m_pitch, target);
    CameraPose pose;
    pose.eye = Lerp(before.eye, now.eye, m_blend);
    pose.forward = NormalizeOrZero(Lerp(before.forward, now.forward, m_blend));
    if (LengthSquared(pose.forward) == 0.0f)
    {
        pose.forward = now.forward;
    }
    pose.fov = before.fov + (now.fov - before.fov) * m_blend;
    return pose;
}

const CameraRig* CameraController::Active() const noexcept
{
    return m_active < m_rigs.size() ? &m_rigs[m_active] : nullptr;
}

std::vector<CameraSwitchEvent> CameraController::TakeEvents()
{
    return std::exchange(m_events, {});
}

} // namespace interi
