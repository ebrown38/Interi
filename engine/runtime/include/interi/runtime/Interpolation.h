// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>

namespace interi
{

/// Blends two simulated states for rendering between fixed steps: `alpha` = FixedStepClock::Alpha(), 0 gives
/// `previous`, 1 gives `current`. Position and scale are lerped, rotation slerped along the shorter arc.
[[nodiscard]] inline Transform Interpolate(const Transform& previous, const Transform& current, float alpha) noexcept
{
    Transform result;
    result.position = Lerp(previous.position, current.position, alpha);
    result.rotation = Slerp(previous.rotation, current.rotation, alpha);
    result.scale = Lerp(previous.scale, current.scale, alpha);
    return result;
}

} // namespace interi
