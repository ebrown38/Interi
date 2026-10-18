// SPDX-License-Identifier: MIT
#pragma once

// Parametric primitives. Every shape is centred on its bounding box (the plane lies in y = 0) and, except the
// plane, is a closed, outward-facing triangle mesh: positions are welded (seams and poles share one vertex), so
// every edge is used by exactly two triangles in opposite directions. Normals are per corner (flat faces get the
// face normal, curved surfaces a smooth one), and each triangle has a material slot (its face group).
//
// Generation uses std::sin/std::cos, so meshes may differ in the last bits between standard libraries: compare
// them by properties (closed, volume), never bit for bit, and never feed them to deterministic simulation.

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/scene/Mesh.h>
#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::scene
{

enum class PrimitiveShape : std::uint8_t
{
    Box,
    Sphere,
    Cylinder,
    Cone,
    Capsule,
    Plane,
    Wedge,
    Stairs,
    Torus,
};

inline constexpr std::size_t kPrimitiveShapeCount = 9;

[[nodiscard]] std::string_view ToString(PrimitiveShape shape) noexcept;
[[nodiscard]] std::optional<PrimitiveShape> ParsePrimitiveShape(std::string_view name) noexcept;
/// The shape names in enum order (the PrimitiveComponent "shape" options).
[[nodiscard]] std::vector<std::string> PrimitiveShapeNames();

/// The parameters of a primitive. Which ones a shape reads:
///   Box, Wedge, Plane (x, z): size. Stairs: size, steps.
///   Sphere: radius, segments, rings. Cylinder, Cone: radius, height, segments.
///   Capsule: radius, height (total, > 2 radius), segments, rings (per hemisphere).
///   Torus: radius (ring centre), tube (tube radius), segments (around the ring), rings (around the tube).
struct PrimitiveParams
{
    PrimitiveShape shape = PrimitiveShape::Box;
    Vec3 size{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    float tube = 0.2f;
    std::uint32_t segments = 24;
    std::uint32_t rings = 12;
    std::uint32_t steps = 4;
};

/// The face groups of a shape, by slot ("Side", "Top", ...).
[[nodiscard]] std::span<const std::string_view> PrimitiveSlots(PrimitiveShape shape) noexcept;
/// Whether the shape is closed (everything but the plane).
[[nodiscard]] constexpr bool IsClosed(PrimitiveShape shape) noexcept
{
    return shape != PrimitiveShape::Plane;
}

/// Half the extent of the shape's bounding box (centred on the origin; a plane's y is 0). Exact for every shape.
[[nodiscard]] Vec3 PrimitiveHalfExtents(const PrimitiveParams& params) noexcept;

/// Generates the mesh. InvalidArgument for a non-positive or non-finite dimension a shape reads, too few segments
/// (3), rings (2 for spheres, 1 otherwise) or steps (1), more than 1024 segments, rings or steps, a capsule no
/// taller than its diameter, or a torus tube not thinner than its radius.
[[nodiscard]] Result<TriangleMesh> GeneratePrimitive(const PrimitiveParams& params);

/// A primitive object's shape, placed at `offset` in the object's space (pivot editing moves the object's origin
/// without moving its geometry). Slot i is drawn with colors[min(i, size - 1)] (grey if empty).
struct PrimitiveComponent
{
    static constexpr const char* kComponentName = "interi.Primitive";
    values::EnumValue shape{.name = "Box"};
    Vec3 size{1.0f};
    float radius = 0.5f;
    float height = 1.0f;
    float tube = 0.2f;
    std::uint32_t segments = 24;
    std::uint32_t rings = 12;
    std::uint32_t steps = 4;
    values::ValueList colors{.element = values::ValueType::Color, .items = {}};
    Vec3 offset;
};

/// The component's parameters (InvalidArgument for an unknown shape name).
[[nodiscard]] Result<PrimitiveParams> ParamsOf(const PrimitiveComponent& component);
[[nodiscard]] PrimitiveComponent MakePrimitiveComponent(const PrimitiveParams& params);

/// Registers PrimitiveComponent (shape as an Enum with the shape names; dimensions with a small positive minimum).
void RegisterPrimitiveReflection(values::ReflectionRegistry& registry);

} // namespace interi::scene
