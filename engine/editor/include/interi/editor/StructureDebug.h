// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>

#include <vector>

namespace interi::editor
{

/// The stress heatmap colour of a bond utilization (load over its health-scaled limit): green at 0, yellow at 0.5,
/// red at 1 and above (linear in between). Negative and NaN values count as 0.
[[nodiscard]] Vec3 StressOverlayColor(float utilization) noexcept;

/// One bond of the overlay: from chunk A's centroid to chunk B's, where their pieces are now.
struct BondLine
{
    Vec3 from;
    Vec3 to;
    float utilization = 0.0f;
};

/// The intact bonds of a destructible instance with their utilization (BondUtilization: 0 until a stress solve has
/// converged).
[[nodiscard]] std::vector<BondLine> StressOverlayLines(const World& world,
                                                       const destruction::DestructionWorld& destruction,
                                                       destruction::DestructibleId id);

/// World positions of the instance's anchor chunks (what holds it to the ground).
[[nodiscard]] std::vector<Vec3> AnchorPoints(const World& world, const destruction::DestructionWorld& destruction,
                                             destruction::DestructibleId id);

} // namespace interi::editor
