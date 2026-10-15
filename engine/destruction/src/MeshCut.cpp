// SPDX-License-Identifier: MIT
#include <interi/destruction/FractureMesh.h>

#include "Triangulate.h"
#include "Vec3d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace interi::destruction
{
namespace
{

using detail::Vec3d;

constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b) noexcept
{
    return (static_cast<std::uint64_t>(a) << 32) | b;
}

/// Builds the output of one cut: output positions are created on first use (original positions keep their
/// relative order of first use; crossing-edge positions are created once per edge).
class Cutter
{
public:
    Cutter(const FractureMesh& mesh, const CutPlane& plane, double tolerance)
        : m_mesh(mesh)
    {
        const double length = std::sqrt(plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] +
                                        plane.normal[2] * plane.normal[2]);
        m_normal = {plane.normal[0] / length, plane.normal[1] / length, plane.normal[2] / length};
        const double d = plane.d / length;

        m_distance.resize(mesh.positions.size());
        m_side.resize(mesh.positions.size());
        for (std::size_t i = 0; i < mesh.positions.size(); ++i)
        {
            const double distance = detail::Dot(m_normal, Vec3d::From(mesh.positions[i])) + d;
            m_distance[i] = distance;
            m_side[i] = distance > tolerance ? 1 : (distance < -tolerance ? -1 : 0);
        }
        m_outputIndex.assign(mesh.positions.size(), kNone);
    }

    FractureMesh Run()
    {
        for (const FractureTriangle& triangle : m_mesh.triangles)
        {
            ClipTriangle(triangle);
        }
        AddCaps();
        return std::move(m_out);
    }

private:
    struct Corner
    {
        std::uint32_t index; // output position
        Vec3 normal;
        Uv uv;
    };

    std::uint32_t OutputOf(std::uint32_t original)
    {
        if (m_outputIndex[original] == kNone)
        {
            m_outputIndex[original] = static_cast<std::uint32_t>(m_out.positions.size());
            m_out.positions.push_back(m_mesh.positions[original]);
        }
        return m_outputIndex[original];
    }

    /// The position where edge (a, b) crosses the plane, created once per undirected edge. Interpolates from the
    /// lower to the higher original index so both triangles sharing the edge compute the same point.
    std::uint32_t EdgePoint(std::uint32_t a, std::uint32_t b)
    {
        const std::uint32_t lo = std::min(a, b);
        const std::uint32_t hi = std::max(a, b);
        const auto [it, inserted] = m_edgePoints.try_emplace(EdgeKey(lo, hi), kNone);
        if (inserted)
        {
            const double t = m_distance[lo] / (m_distance[lo] - m_distance[hi]);
            const Vec3d pLo = Vec3d::From(m_mesh.positions[lo]);
            const Vec3d pHi = Vec3d::From(m_mesh.positions[hi]);
            it->second = static_cast<std::uint32_t>(m_out.positions.size());
            m_out.positions.push_back((pLo + (pHi - pLo) * t).ToFloat());
        }
        return it->second;
    }

    void ClipTriangle(const FractureTriangle& triangle)
    {
        const auto& idx = triangle.indices;
        int behind = 0;
        int front = 0;
        for (const std::uint32_t i : idx)
        {
            behind += m_side[i] < 0 ? 1 : 0;
            front += m_side[i] > 0 ? 1 : 0;
        }
        if (front == 0 && behind == 0)
        {
            // Lies on the plane: it bounds the kept solid only if it faces along the normal.
            const Vec3d a = Vec3d::From(m_mesh.positions[idx[0]]);
            const Vec3d faceNormal =
                detail::Cross(Vec3d::From(m_mesh.positions[idx[1]]) - a, Vec3d::From(m_mesh.positions[idx[2]]) - a);
            if (detail::Dot(faceNormal, m_normal) > 0.0)
            {
                Emit(triangle, {Original(triangle, 0), Original(triangle, 1), Original(triangle, 2)});
            }
            return;
        }
        if (front == 0)
        {
            Emit(triangle, {Original(triangle, 0), Original(triangle, 1), Original(triangle, 2)});
            return;
        }
        if (behind == 0)
        {
            return;
        }

        // Sutherland-Hodgman on the triangle: at most four corners survive.
        std::array<Corner, 4> polygon{};
        std::size_t count = 0;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const std::size_t next = (k + 1) % 3;
            const int sideK = m_side[idx[k]];
            const int sideNext = m_side[idx[next]];
            if (sideK <= 0)
            {
                polygon[count++] = Original(triangle, k);
            }
            if (sideK * sideNext < 0)
            {
                polygon[count++] = Crossing(triangle, k, next);
            }
        }
        Emit(triangle, {polygon[0], polygon[1], polygon[2]});
        if (count == 4)
        {
            Emit(triangle, {polygon[0], polygon[2], polygon[3]});
        }
    }

    Corner Original(const FractureTriangle& triangle, std::size_t k)
    {
        return {OutputOf(triangle.indices[k]), triangle.normals[k], triangle.uvs[k]};
    }

    Corner Crossing(const FractureTriangle& triangle, std::size_t from, std::size_t to)
    {
        const std::uint32_t a = triangle.indices[from];
        const std::uint32_t b = triangle.indices[to];
        const std::uint32_t lo = std::min(a, b);
        const std::uint32_t hi = std::max(a, b);
        const double tLo = m_distance[lo] / (m_distance[lo] - m_distance[hi]);
        const auto t = static_cast<float>(a == lo ? tLo : 1.0 - tLo); // parameter from corner `from`
        const Uv& uvA = triangle.uvs[from];
        const Uv& uvB = triangle.uvs[to];
        return {EdgePoint(a, b), NormalizeOrZero(Lerp(triangle.normals[from], triangle.normals[to], t)),
                Uv{uvA.u + (uvB.u - uvA.u) * t, uvA.v + (uvB.v - uvA.v) * t}};
    }

    void Emit(const FractureTriangle& source, const std::array<Corner, 3>& corners)
    {
        if (corners[0].index == corners[1].index || corners[1].index == corners[2].index ||
            corners[2].index == corners[0].index)
        {
            return; // collapsed onto the plane
        }
        FractureTriangle t = source;
        for (std::size_t k = 0; k < 3; ++k)
        {
            t.indices[k] = corners[k].index;
            t.normals[k] = corners[k].normal;
            t.uvs[k] = corners[k].uv;
        }
        m_out.triangles.push_back(t);
    }

    /// Closes every opening: boundary edges (used in one direction only) all lie on the plane. Cap faces use them
    /// reversed; their loops are outer boundaries (counter-clockwise seen along the normal) and holes.
    void AddCaps()
    {
        const auto keptTriangles = m_out.triangles.size();
        std::unordered_map<std::uint64_t, std::uint32_t> directed; // edge -> triangle
        directed.reserve(keptTriangles * 3);
        for (std::uint32_t t = 0; t < keptTriangles; ++t)
        {
            const auto& indices = m_out.triangles[t].indices;
            for (std::size_t k = 0; k < 3; ++k)
            {
                directed.emplace(EdgeKey(indices[k], indices[(k + 1) % 3]), t);
            }
        }

        // Cap edge b -> a for each boundary edge a -> b, collected in triangle order for determinism.
        struct CapEdge
        {
            std::uint32_t from;
            std::uint32_t to;
            std::uint32_t triangle;
        };
        std::vector<CapEdge> capEdges;
        for (std::uint32_t t = 0; t < keptTriangles; ++t)
        {
            const auto& indices = m_out.triangles[t].indices;
            for (std::size_t k = 0; k < 3; ++k)
            {
                const std::uint32_t a = indices[k];
                const std::uint32_t b = indices[(k + 1) % 3];
                if (!directed.contains(EdgeKey(b, a)))
                {
                    capEdges.push_back({b, a, t});
                }
            }
        }
        if (capEdges.empty())
        {
            return;
        }
        std::ranges::sort(capEdges, [](const CapEdge& x, const CapEdge& y)
                          { return x.from != y.from ? x.from < y.from : x.to < y.to; });

        // Chain into loops.
        std::vector<bool> used(capEdges.size(), false);
        std::vector<std::vector<std::uint32_t>> loops; // output position indices
        std::vector<std::uint32_t> loopMaterialTriangle;
        const auto findFrom = [&](std::uint32_t from) -> std::size_t
        {
            auto it = std::ranges::lower_bound(capEdges, from, {}, &CapEdge::from);
            for (; it != capEdges.end() && it->from == from; ++it)
            {
                const auto e = static_cast<std::size_t>(it - capEdges.begin());
                if (!used[e])
                {
                    return e;
                }
            }
            return capEdges.size();
        };
        for (std::size_t start = 0; start < capEdges.size(); ++start)
        {
            if (used[start])
            {
                continue;
            }
            std::vector<std::uint32_t> loop;
            std::size_t e = start;
            while (e != capEdges.size() && !used[e])
            {
                used[e] = true;
                loop.push_back(capEdges[e].from);
                if (capEdges[e].to == capEdges[start].from)
                {
                    break;
                }
                e = findFrom(capEdges[e].to);
            }
            if (loop.size() >= 3)
            {
                loops.push_back(std::move(loop));
                loopMaterialTriangle.push_back(capEdges[start].triangle);
            }
        }

        // Project onto the plane: (u, v, normal) is right-handed, so counter-clockwise in (u, v) faces the normal.
        const Vec3d helper = std::abs(m_normal.x) < 0.6 ? Vec3d{1, 0, 0} : Vec3d{0, 1, 0};
        Vec3d u = detail::Cross(helper, m_normal);
        u = u * (1.0 / detail::Length(u));
        const Vec3d v = detail::Cross(m_normal, u);

        std::vector<detail::Point2> points;
        std::vector<std::uint32_t> pointToOutput;
        std::unordered_map<std::uint32_t, std::uint32_t> outputToPoint;
        std::vector<std::vector<std::uint32_t>> localLoops;
        for (const auto& loop : loops)
        {
            std::vector<std::uint32_t> local;
            for (const std::uint32_t index : loop)
            {
                const auto [it, inserted] = outputToPoint.try_emplace(index, static_cast<std::uint32_t>(points.size()));
                if (inserted)
                {
                    const Vec3d p = Vec3d::From(m_out.positions[index]);
                    points.push_back({detail::Dot(p, u), detail::Dot(p, v)});
                    pointToOutput.push_back(index);
                }
                local.push_back(it->second);
            }
            localLoops.push_back(std::move(local));
        }

        // A cap point's material comes from the loop it was first seen in.
        std::vector<std::uint32_t> materialOfPoint(points.size(), 0);
        std::vector<bool> materialSet(points.size(), false);
        for (std::size_t l = 0; l < localLoops.size(); ++l)
        {
            for (const std::uint32_t p : localLoops[l])
            {
                if (!materialSet[p])
                {
                    materialSet[p] = true;
                    materialOfPoint[p] = m_out.triangles[loopMaterialTriangle[l]].material;
                }
            }
        }

        const Vec3 capNormal = m_normal.ToFloat();
        for (const auto& [a, b, c] : detail::TriangulateLoops(points, localLoops))
        {
            FractureTriangle t;
            t.indices = {pointToOutput[a], pointToOutput[b], pointToOutput[c]};
            t.normals = {capNormal, capNormal, capNormal};
            t.uvs = {ToUv(points[a]), ToUv(points[b]), ToUv(points[c])};
            t.material = materialOfPoint[a];
            t.interior = true;
            m_out.triangles.push_back(t);
        }
    }

    static Uv ToUv(const detail::Point2& p) noexcept { return {static_cast<float>(p.x), static_cast<float>(p.y)}; }

    const FractureMesh& m_mesh;
    Vec3d m_normal;
    std::vector<double> m_distance;
    std::vector<int> m_side;
    std::vector<std::uint32_t> m_outputIndex;
    std::unordered_map<std::uint64_t, std::uint32_t> m_edgePoints;
    FractureMesh m_out;
};

} // namespace

FractureMesh CutMesh(const FractureMesh& mesh, const CutPlane& plane, double tolerance)
{
    return Cutter(mesh, plane, tolerance).Run();
}

} // namespace interi::destruction
