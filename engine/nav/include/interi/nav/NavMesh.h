// SPDX-License-Identifier: MIT
#pragma once

// Navigation meshes (M24): baked from triangle geometry with Recast, queried with Detour (zlib licensed). A NavMesh is
// immutable once built, so any number of threads may query it at once: every query makes its own Detour query object.
//
// Baking is an offline/load-time step (the editor or a cook); gameplay only reads. The baked data can be written to a
// `.inav` file (magic, payload hash, versioned archive) and loaded back, bit-identical.
//
// Recast and Detour use float math the engine's determinism rules don't pin, so a bake is reproducible on one
// platform and toolchain, not across them: ship the cooked `.inav`, don't rebake on the client.

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class dtNavMesh;

namespace interi::nav
{

/// The agent a mesh is baked for and the voxelization.
struct NavBakeSettings
{
    float cellSize = 0.3f;
    float cellHeight = 0.2f;
    float agentHeight = 2.0f;
    float agentRadius = 0.6f;
    float agentMaxClimb = 0.9f;
    float agentMaxSlopeDegrees = 45.0f;
    float maxEdgeLength = 12.0f;
    float maxSimplificationError = 1.3f;
    std::int32_t minRegionArea = 8;
    std::int32_t mergeRegionArea = 20;
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;
};

/// A route: the waypoints from start to end (both on the mesh).
struct NavPath
{
    std::vector<Vec3> points;
    /// The goal is not reachable; the route ends at the closest reachable point instead.
    bool partial = false;
    [[nodiscard]] float Length() const noexcept;
};

class NavMesh
{
public:
    NavMesh(NavMesh&&) noexcept;
    NavMesh& operator=(NavMesh&&) noexcept;
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    /// Bakes triangles (indices in groups of three, winding counter-clockwise seen from above for walkable floors).
    /// InvalidArgument for bad settings or indices, Degenerate when nothing walkable remains.
    static Result<NavMesh> Bake(std::span<const Vec3> vertices, std::span<const std::uint32_t> indices,
                                const NavBakeSettings& settings = {});

    /// The `.inav` file.
    [[nodiscard]] std::vector<std::byte> Write() const;
    /// Corrupt for a damaged file, VersionMismatch for a newer one.
    static Result<NavMesh> Read(std::span<const std::byte> bytes);

    /// The closest point on the mesh within `extents` (half sizes) of `point`; NotFound when there is none.
    [[nodiscard]] Result<Vec3> NearestPoint(const Vec3& point, const Vec3& extents = Vec3(2.0f, 4.0f, 2.0f)) const;
    /// The shortest route. Both ends snap to the mesh within `extents`; NotFound when either is off the mesh. A goal in
    /// another island gives a partial path to the closest reachable point.
    [[nodiscard]] Result<NavPath> FindPath(const Vec3& from, const Vec3& to,
                                           const Vec3& extents = Vec3(2.0f, 4.0f, 2.0f)) const;

    [[nodiscard]] std::size_t PolygonCount() const noexcept;
    [[nodiscard]] const NavBakeSettings& Settings() const noexcept { return m_settings; }

private:
    struct Deleter
    {
        void operator()(dtNavMesh* mesh) const noexcept;
    };

    NavMesh(std::unique_ptr<dtNavMesh, Deleter> mesh, std::vector<std::byte> data, const NavBakeSettings& settings);
    static Result<NavMesh> FromData(std::vector<std::byte> data, const NavBakeSettings& settings);

    std::unique_ptr<dtNavMesh, Deleter> m_mesh;
    std::vector<std::byte> m_data; ///< The Detour tile; the mesh points into a copy of it.
    NavBakeSettings m_settings;
};

} // namespace interi::nav
