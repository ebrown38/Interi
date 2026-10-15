// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>

#include "Triangulate.h"
#include "Vec3d.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace interi::destruction
{
namespace
{

using detail::Vec3d;

std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b) noexcept
{
    return (static_cast<std::uint64_t>(a) << 32) | b;
}

Vec3d Reference(const FractureMesh& mesh) noexcept
{
    return Vec3d::From(ComputeBounds(mesh).Center());
}

std::uint32_t FindRoot(std::vector<std::uint32_t>& parent, std::uint32_t x) noexcept
{
    while (parent[x] != x)
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

/// Copies the listed triangles into a new mesh holding only the positions they use, in original order.
FractureMesh ExtractTriangles(const FractureMesh& mesh, std::span<const std::uint32_t> triangles)
{
    constexpr std::uint32_t kUnused = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> remap(mesh.positions.size(), kUnused);
    for (const std::uint32_t t : triangles)
    {
        for (const std::uint32_t index : mesh.triangles[t].indices)
        {
            remap[index] = 0;
        }
    }
    FractureMesh out;
    for (std::size_t i = 0; i < remap.size(); ++i)
    {
        if (remap[i] != kUnused)
        {
            remap[i] = static_cast<std::uint32_t>(out.positions.size());
            out.positions.push_back(mesh.positions[i]);
        }
    }
    out.triangles.reserve(triangles.size());
    for (const std::uint32_t t : triangles)
    {
        FractureTriangle triangle = mesh.triangles[t];
        for (std::uint32_t& index : triangle.indices)
        {
            index = remap[index];
        }
        out.triangles.push_back(triangle);
    }
    return out;
}

} // namespace

double ComputeVolume(const FractureMesh& mesh) noexcept
{
    const Vec3d r = Reference(mesh);
    double volume = 0.0;
    for (const FractureTriangle& t : mesh.triangles)
    {
        const Vec3d a = Vec3d::From(mesh.positions[t.indices[0]]) - r;
        const Vec3d b = Vec3d::From(mesh.positions[t.indices[1]]) - r;
        const Vec3d c = Vec3d::From(mesh.positions[t.indices[2]]) - r;
        volume += detail::Dot(a, detail::Cross(b, c));
    }
    return volume / 6.0;
}

Vec3 ComputeCentroid(const FractureMesh& mesh) noexcept
{
    const Vec3d r = Reference(mesh);
    double volume = 0.0;
    Vec3d moment;
    for (const FractureTriangle& t : mesh.triangles)
    {
        const Vec3d a = Vec3d::From(mesh.positions[t.indices[0]]) - r;
        const Vec3d b = Vec3d::From(mesh.positions[t.indices[1]]) - r;
        const Vec3d c = Vec3d::From(mesh.positions[t.indices[2]]) - r;
        const double v = detail::Dot(a, detail::Cross(b, c));
        volume += v;
        moment = moment + (a + b + c) * (v * 0.25); // tetrahedron (r, a, b, c) centroid relative to r
    }
    if (std::abs(volume) <= 0.0)
    {
        return r.ToFloat();
    }
    return (r + moment * (1.0 / volume)).ToFloat();
}

AABB ComputeBounds(const FractureMesh& mesh) noexcept
{
    return AABB::FromPoints(mesh.positions);
}

double ComputeInteriorArea(const FractureMesh& mesh) noexcept
{
    double area = 0.0;
    for (const FractureTriangle& t : mesh.triangles)
    {
        if (!t.interior)
        {
            continue;
        }
        const Vec3d a = Vec3d::From(mesh.positions[t.indices[0]]);
        const Vec3d b = Vec3d::From(mesh.positions[t.indices[1]]);
        const Vec3d c = Vec3d::From(mesh.positions[t.indices[2]]);
        area += 0.5 * detail::Length(detail::Cross(b - a, c - a));
    }
    return area;
}

bool IsWatertight(const FractureMesh& mesh)
{
    std::vector<std::uint64_t> edges;
    edges.reserve(mesh.triangles.size() * 3);
    for (const FractureTriangle& t : mesh.triangles)
    {
        for (int k = 0; k < 3; ++k)
        {
            const std::uint32_t a = t.indices[static_cast<std::size_t>(k)];
            const std::uint32_t b = t.indices[static_cast<std::size_t>((k + 1) % 3)];
            if (a == b)
            {
                return false;
            }
            edges.push_back(EdgeKey(a, b));
        }
    }
    std::ranges::sort(edges);
    if (std::ranges::adjacent_find(edges) != edges.end())
    {
        return false; // a directed edge used twice
    }
    return std::ranges::all_of(edges,
                               [&](std::uint64_t e)
                               {
                                   const auto reversed =
                                       EdgeKey(static_cast<std::uint32_t>(e), static_cast<std::uint32_t>(e >> 32));
                                   return std::ranges::binary_search(edges, reversed);
                               });
}

bool ContainsPoint(const FractureMesh& mesh, const Vec3& point) noexcept
{
    // An irrational-looking direction, so rays practically never graze edges or vertices of axis-aligned input.
    const Vec3d direction{0.4364357804719848, 0.7637626158259733, 0.4755282581475768};
    const Vec3d origin = Vec3d::From(point);
    int crossings = 0;
    for (const FractureTriangle& t : mesh.triangles)
    {
        const Vec3d a = Vec3d::From(mesh.positions[t.indices[0]]);
        const Vec3d edge1 = Vec3d::From(mesh.positions[t.indices[1]]) - a;
        const Vec3d edge2 = Vec3d::From(mesh.positions[t.indices[2]]) - a;
        const Vec3d p = detail::Cross(direction, edge2);
        const double determinant = detail::Dot(edge1, p);
        if (determinant == 0.0)
        {
            continue;
        }
        const double inverse = 1.0 / determinant;
        const Vec3d s = origin - a;
        const double u = detail::Dot(s, p) * inverse;
        if (u < 0.0 || u > 1.0)
        {
            continue;
        }
        const Vec3d q = detail::Cross(s, edge1);
        const double v = detail::Dot(direction, q) * inverse;
        if (v < 0.0 || u + v > 1.0)
        {
            continue;
        }
        if (detail::Dot(edge2, q) * inverse > 0.0)
        {
            ++crossings;
        }
    }
    return (crossings & 1) != 0;
}

std::vector<FractureMesh> SplitConnectedComponents(const FractureMesh& mesh)
{
    const auto triangleCount = static_cast<std::uint32_t>(mesh.triangles.size());
    std::vector<std::uint32_t> parent(triangleCount);
    std::ranges::iota(parent, 0u);

    // Triangles sharing an undirected edge belong together.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> edges;
    edges.reserve(mesh.triangles.size() * 3);
    for (std::uint32_t t = 0; t < triangleCount; ++t)
    {
        const auto& indices = mesh.triangles[t].indices;
        for (int k = 0; k < 3; ++k)
        {
            const std::uint32_t a = indices[static_cast<std::size_t>(k)];
            const std::uint32_t b = indices[static_cast<std::size_t>((k + 1) % 3)];
            edges.emplace_back(EdgeKey(std::min(a, b), std::max(a, b)), t);
        }
    }
    std::ranges::sort(edges);
    for (std::size_t i = 1; i < edges.size(); ++i)
    {
        if (edges[i].first == edges[i - 1].first)
        {
            const std::uint32_t ra = FindRoot(parent, edges[i].second);
            const std::uint32_t rb = FindRoot(parent, edges[i - 1].second);
            parent[std::max(ra, rb)] = std::min(ra, rb); // root = lowest triangle index
        }
    }

    std::vector<std::vector<std::uint32_t>> groups;
    std::vector<std::uint32_t> groupOfRoot(triangleCount, std::numeric_limits<std::uint32_t>::max());
    for (std::uint32_t t = 0; t < triangleCount; ++t)
    {
        const std::uint32_t root = FindRoot(parent, t);
        if (groupOfRoot[root] == std::numeric_limits<std::uint32_t>::max())
        {
            groupOfRoot[root] = static_cast<std::uint32_t>(groups.size());
            groups.emplace_back();
        }
        groups[groupOfRoot[root]].push_back(t);
    }

    std::vector<FractureMesh> components;
    components.reserve(groups.size());
    for (const auto& group : groups)
    {
        components.push_back(ExtractTriangles(mesh, group));
    }
    return components;
}

FractureMesh CompactPositions(const FractureMesh& mesh)
{
    std::vector<std::uint32_t> all(mesh.triangles.size());
    std::ranges::iota(all, 0u);
    return ExtractTriangles(mesh, all);
}

FractureMesh WeldIdenticalPositions(const FractureMesh& mesh)
{
    // -0.0 and 0.0 are the same point.
    const auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f == 0.0f ? 0.0f : f); };
    std::vector<std::uint32_t> order(mesh.positions.size());
    std::ranges::iota(order, 0u);
    const auto key = [&](std::uint32_t i)
    {
        const Vec3& p = mesh.positions[i];
        return std::tuple(bits(p.x), bits(p.y), bits(p.z), i);
    };
    std::ranges::sort(order, [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });

    std::vector<std::uint32_t> canonical(mesh.positions.size());
    for (std::size_t k = 0; k < order.size(); ++k)
    {
        const bool same = k > 0 && std::get<0>(key(order[k])) == std::get<0>(key(order[k - 1])) &&
                          std::get<1>(key(order[k])) == std::get<1>(key(order[k - 1])) &&
                          std::get<2>(key(order[k])) == std::get<2>(key(order[k - 1]));
        canonical[order[k]] = same ? canonical[order[k - 1]] : order[k]; // lowest index of the group
    }

    FractureMesh welded = mesh;
    for (FractureTriangle& t : welded.triangles)
    {
        for (std::uint32_t& index : t.indices)
        {
            index = canonical[index];
        }
    }
    return CompactPositions(welded);
}

double CutTolerance(const FractureMesh& mesh) noexcept
{
    double magnitude = 0.0;
    for (const Vec3& p : mesh.positions)
    {
        magnitude = std::max({magnitude, static_cast<double>(std::abs(p.x)), static_cast<double>(std::abs(p.y)),
                              static_cast<double>(std::abs(p.z))});
    }
    return std::max(magnitude, 1.0) * 1e-6;
}

FractureMesh MakeBoxMesh(const Vec3& min, const Vec3& max, std::uint32_t material)
{
    FractureMesh mesh;
    for (int i = 0; i < 8; ++i)
    {
        mesh.positions.emplace_back((i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y,
                                    (i & 4) != 0 ? max.z : min.z);
    }
    // Each face: four corners counter-clockwise seen from outside, its normal, and the axes its UVs follow.
    struct Face
    {
        std::array<std::uint32_t, 4> corners;
        Vec3 normal;
        int uAxis;
        int vAxis;
    };
    constexpr Face kFaces[] = {
        {{0, 4, 6, 2}, {-1, 0, 0}, 2, 1}, {{1, 3, 7, 5}, {1, 0, 0}, 2, 1},  {{0, 1, 5, 4}, {0, -1, 0}, 0, 2},
        {{2, 6, 7, 3}, {0, 1, 0}, 0, 2},  {{0, 2, 3, 1}, {0, 0, -1}, 0, 1}, {{4, 5, 7, 6}, {0, 0, 1}, 0, 1},
    };
    for (const Face& face : kFaces)
    {
        const auto uv = [&](std::uint32_t index)
        {
            const Vec3& p = mesh.positions[index];
            return Uv{p[face.uAxis], p[face.vAxis]};
        };
        for (const auto& [i0, i1, i2] : {std::array<std::size_t, 3>{0, 1, 2}, std::array<std::size_t, 3>{0, 2, 3}})
        {
            FractureTriangle t;
            t.indices = {face.corners[i0], face.corners[i1], face.corners[i2]};
            t.normals = {face.normal, face.normal, face.normal};
            t.uvs = {uv(t.indices[0]), uv(t.indices[1]), uv(t.indices[2])};
            t.material = material;
            mesh.triangles.push_back(t);
        }
    }
    return mesh;
}

FractureMesh MakePrismMesh(std::span<const std::array<float, 2>> outline, float zMin, float zMax,
                           std::uint32_t material)
{
    FractureMesh mesh;
    const auto n = static_cast<std::uint32_t>(outline.size());
    for (const float z : {zMin, zMax})
    {
        for (const auto& [x, y] : outline)
        {
            mesh.positions.emplace_back(x, y, z);
        }
    }
    const auto add = [&](std::array<std::uint32_t, 3> indices, const Vec3& normal, int uAxis, int vAxis)
    {
        FractureTriangle t;
        t.indices = indices;
        t.normals = {normal, normal, normal};
        for (std::size_t k = 0; k < 3; ++k)
        {
            const Vec3& p = mesh.positions[indices[k]];
            t.uvs[k] = {p[uAxis], p[vAxis]};
        }
        t.material = material;
        mesh.triangles.push_back(t);
    };

    for (std::uint32_t i = 0; i < n; ++i)
    {
        const std::uint32_t j = (i + 1) % n;
        const Vec3 normal = NormalizeOrZero(Vec3(outline[j][1] - outline[i][1], outline[i][0] - outline[j][0], 0.0f));
        const int uAxis = std::abs(normal.x) > std::abs(normal.y) ? 1 : 0;
        add({i, j, n + j}, normal, uAxis, 2);
        add({i, n + j, n + i}, normal, uAxis, 2);
    }

    std::vector<detail::Point2> points;
    for (const auto& [x, y] : outline)
    {
        points.push_back({static_cast<double>(x), static_cast<double>(y)});
    }
    std::vector<std::uint32_t> loop(n);
    std::ranges::iota(loop, 0u);
    for (const auto& [a, b, c] : detail::TriangulateLoops(points, {loop}))
    {
        add({n + a, n + b, n + c}, {0, 0, 1}, 0, 1);
        add({c, b, a}, {0, 0, -1}, 0, 1);
    }
    return mesh;
}

void AppendMesh(FractureMesh& mesh, const FractureMesh& other)
{
    const auto offset = static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(), other.positions.begin(), other.positions.end());
    for (FractureTriangle t : other.triangles)
    {
        for (std::uint32_t& index : t.indices)
        {
            index += offset;
        }
        mesh.triangles.push_back(t);
    }
}

} // namespace interi::destruction
