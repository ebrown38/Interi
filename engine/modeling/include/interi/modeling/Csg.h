// SPDX-License-Identifier: MIT
#pragma once

// Constructive solid geometry on closed triangle meshes, through Manifold (Apache-2.0). Editor-only: the result is
// baked into a mesh asset, so Manifold never reaches the runtime.

#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/scene/Mesh.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace interi::modeling
{

enum class CsgOperation : std::uint8_t
{
    Union,
    Subtract, ///< The first operand minus every other.
    Intersect,
};

[[nodiscard]] std::string_view ToString(CsgOperation operation) noexcept;

/// A mesh placed in the world.
struct CsgOperand
{
    const scene::TriangleMesh* mesh = nullptr;
    Transform transform; ///< Mesh space to world (a negative scale mirrors).
};

/// Combines the operands, left to right, in world space. The result is a closed mesh with flat normals; triangles
/// keep the slot of the operand they came from (operand i's slots are offset by the slot counts before it).
/// InvalidArgument for fewer than two operands or one that isn't a closed 2-manifold; Degenerate for an empty
/// result.
[[nodiscard]] Result<scene::TriangleMesh> Combine(std::span<const CsgOperand> operands, CsgOperation operation);

} // namespace interi::modeling
