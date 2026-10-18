// SPDX-License-Identifier: MIT
#pragma once

// Camera rigs as data, and a controller that switches between them.
//
//     { "rigs": [ { "name": "orbit", "kind": "orbit", "distance": 5, "shoulder": 0.6, "fov": 70 },
//                 { "name": "eyes",  "kind": "firstPerson", "eyeHeight": 0.0 },
//                 { "name": "cctv",  "kind": "fixed", "position": [0, 6, 8], "lookAt": [0, 0, 0] },
//                 { "name": "map",   "kind": "topDown", "height": 14, "pitch": -1.4 } ] }
//
// Kinds: orbit (FollowCamera: yaw/pitch around the target), firstPerson (at the target, yaw/pitch), fixed (a
// place and a point to look at), topDown (above the target at a fixed pitch, yaw 0 looks along -Z). Angles are
// radians, fov degrees. Unknown fields are warnings, anything malformed is an error.

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{

enum class CameraRigKind : std::uint8_t
{
    Orbit,
    FirstPerson,
    Fixed,
    TopDown
};

struct CameraRig
{
    std::string name;
    CameraRigKind kind = CameraRigKind::Orbit;
    float fov = 70.0f;      ///< Vertical field of view, degrees.
    float distance = 4.0f;  ///< Orbit: m behind the target.
    float shoulder = 0.6f;  ///< Orbit: m to the right of the target.
    float height = 12.0f;   ///< TopDown: m above the target.
    float pitch = -1.2f;    ///< TopDown: fixed pitch (radians, in [-1.5, 0)).
    float eyeHeight = 0.0f; ///< FirstPerson: m above the target.
    float minPitch = -1.4f;
    float maxPitch = 1.2f;
    float sensitivity = 0.003f;
    Vec3 position{0.0f, 5.0f, 5.0f}; ///< Fixed.
    Vec3 lookAt{0.0f, 0.0f, 0.0f};   ///< Fixed.
};

struct CameraPose
{
    Vec3 eye;
    Vec3 forward{0.0f, 0.0f, -1.0f}; ///< Unit.
    float fov = 70.0f;
};

struct CameraRigSet
{
    std::vector<CameraRig> rigs;
    std::vector<std::string> warnings;
};

[[nodiscard]] Result<CameraRigSet> ParseCameraRigs(std::string_view json);

/// A rig's pose for a view angle and the target it follows (the character's eyes).
[[nodiscard]] CameraPose EvaluateRig(const CameraRig& rig, float yaw, float pitch, const Vec3& target) noexcept;

/// "Camera switched": raised by SwitchTo, taken by the game (a chain trigger, a HUD change).
struct CameraSwitchEvent
{
    std::string from;
    std::string to;
};

/// Owns the rigs and the view angles, and blends between rigs over `blendSeconds` when switching. Deterministic: it
/// is driven by the caller's dt and uses no clock.
class CameraController
{
public:
    explicit CameraController(std::vector<CameraRig> rigs = {}, float blendSeconds = 0.4f);

    /// Activates the rig called `name`. False for an unknown name (nothing changes). Switching to the active rig is
    /// not an event.
    bool SwitchTo(std::string_view name);
    /// Next rig in file order (wraps).
    void SwitchNext();

    /// Mouse (or stick) motion in pixels, scaled by the active rig's sensitivity and clamped to its pitch limits.
    void Rotate(float deltaX, float deltaY) noexcept;
    void SetView(float yaw, float pitch) noexcept;
    [[nodiscard]] float Yaw() const noexcept { return m_yaw; }
    [[nodiscard]] float Pitch() const noexcept { return m_pitch; }

    /// Advances the blend by `dt` seconds.
    void Update(float dt) noexcept;
    /// The pose to render: the active rig, blended from the previous one right after a switch.
    [[nodiscard]] CameraPose Pose(const Vec3& target) const noexcept;

    [[nodiscard]] const CameraRig* Active() const noexcept;
    [[nodiscard]] std::size_t ActiveIndex() const noexcept { return m_active; }
    [[nodiscard]] bool Blending() const noexcept { return m_blend < 1.0f; }
    [[nodiscard]] const std::vector<CameraRig>& Rigs() const noexcept { return m_rigs; }
    /// Events since the last call, in order.
    [[nodiscard]] std::vector<CameraSwitchEvent> TakeEvents();

private:
    std::vector<CameraRig> m_rigs;
    float m_blendSeconds;
    std::size_t m_active = 0;
    std::size_t m_previous = 0;
    float m_blend = 1.0f; ///< 0..1; 1 = fully on the active rig.
    float m_yaw = 0.0f;
    float m_pitch = -0.25f;
    std::vector<CameraSwitchEvent> m_events;
};

} // namespace interi
