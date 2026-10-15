// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/BondGraph.h>

#include "Vec3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace interi::destruction
{
namespace
{

using detail::Vec3d;

constexpr double kMaxGridResolution = 256.0; // cells along the largest extent, at most
constexpr int kAxisBits = 21;

struct ContactTriangle
{
    std::uint32_t mesh = 0;
    std::array<Vec3d, 3> corners;
    Vec3d normal;        // unit
    double offset = 0.0; // Dot(normal, p) for points p on the triangle's plane
    Vec3d min;
    Vec3d max;
};

struct Accumulator
{
    double area = 0.0;
    Vec3d centroid; // area-weighted sum
    Vec3d normal;   // area-weighted sum, from the lower mesh into the higher
};

struct Point2
{
    double x;
    double y;
};

double Cross2(const Point2& a, const Point2& b, const Point2& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// Area and centroid of the overlap of two coplanar triangles: `clipped` projected onto `reference`'s plane and
/// clipped by its three edges (Sutherland-Hodgman). Returns zero area when they do not overlap.
std::pair<double, Vec3d> Overlap(const ContactTriangle& reference, const ContactTriangle& clipped)
{
    const Vec3d& n = reference.normal;
    const Vec3d helper = std::abs(n.x) < 0.6 ? Vec3d{1, 0, 0} : Vec3d{0, 1, 0};
    Vec3d u = detail::Cross(helper, n);
    u = u * (1.0 / detail::Length(u));
    const Vec3d v = detail::Cross(n, u);
    const auto project = [&](const Vec3d& p) { return Point2{detail::Dot(p, u), detail::Dot(p, v)}; };

    std::array<Point2, 3> edgePoints{};
    for (std::size_t k = 0; k < 3; ++k)
    {
        edgePoints[k] = project(reference.corners[k]);
    }
    std::vector<Point2> polygon{project(clipped.corners[0]), project(clipped.corners[1]), project(clipped.corners[2])};
    std::vector<Point2> next;
    for (std::size_t k = 0; k < 3 && !polygon.empty(); ++k)
    {
        const Point2& a = edgePoints[k];
        const Point2& b = edgePoints[(k + 1) % 3];
        next.clear();
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            const Point2& p = polygon[i];
            const Point2& q = polygon[(i + 1) % polygon.size()];
            const double dp = Cross2(a, b, p);
            const double dq = Cross2(a, b, q);
            if (dp >= 0.0)
            {
                next.push_back(p);
            }
            if ((dp >= 0.0) != (dq >= 0.0))
            {
                const double t = dp / (dp - dq);
                next.push_back({p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t});
            }
        }
        polygon.swap(next);
    }
    if (polygon.size() < 3)
    {
        return {0.0, {}};
    }

    double twiceArea = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const Point2& p = polygon[i];
        const Point2& q = polygon[(i + 1) % polygon.size()];
        const double cross = p.x * q.y - q.x * p.y;
        twiceArea += cross;
        cx += (p.x + q.x) * cross;
        cy += (p.y + q.y) * cross;
    }
    if (std::abs(twiceArea) <= 0.0)
    {
        return {0.0, {}};
    }
    cx /= 3.0 * twiceArea;
    cy /= 3.0 * twiceArea;
    return {std::abs(twiceArea) * 0.5, u * cx + v * cy + n * reference.offset};
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

} // namespace

std::vector<Contact> FindContacts(std::span<const FractureMesh* const> meshes, const ContactSettings& settings)
{
    std::vector<ContactTriangle> triangles;
    Vec3d boundsMin{1e300, 1e300, 1e300};
    Vec3d boundsMax{-1e300, -1e300, -1e300};
    for (std::uint32_t m = 0; m < meshes.size(); ++m)
    {
        for (const FractureTriangle& t : meshes[m]->triangles)
        {
            ContactTriangle triangle;
            triangle.mesh = m;
            for (std::size_t k = 0; k < 3; ++k)
            {
                triangle.corners[k] = Vec3d::From(meshes[m]->positions[t.indices[k]]);
            }
            const Vec3d cross =
                detail::Cross(triangle.corners[1] - triangle.corners[0], triangle.corners[2] - triangle.corners[0]);
            const double length = detail::Length(cross);
            if (length <= 1e-12)
            {
                continue; // degenerate: no area to contribute
            }
            triangle.normal = cross * (1.0 / length);
            triangle.offset = detail::Dot(triangle.normal, triangle.corners[0]);
            triangle.min = triangle.max = triangle.corners[0];
            for (const Vec3d& c : triangle.corners)
            {
                triangle.min = {std::min(triangle.min.x, c.x), std::min(triangle.min.y, c.y),
                                std::min(triangle.min.z, c.z)};
                triangle.max = {std::max(triangle.max.x, c.x), std::max(triangle.max.y, c.y),
                                std::max(triangle.max.z, c.z)};
            }
            boundsMin = {std::min(boundsMin.x, triangle.min.x), std::min(boundsMin.y, triangle.min.y),
                         std::min(boundsMin.z, triangle.min.z)};
            boundsMax = {std::max(boundsMax.x, triangle.max.x), std::max(boundsMax.y, triangle.max.y),
                         std::max(boundsMax.z, triangle.max.z)};
            triangles.push_back(triangle);
        }
    }
    if (triangles.empty())
    {
        return {};
    }

    // Spatial hash: each triangle goes into every grid cell its (tolerance-grown) box overlaps.
    const double tolerance = settings.distanceTolerance;
    const double extent =
        std::max({boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y, boundsMax.z - boundsMin.z, tolerance});
    // Cells about as large as an average triangle: each triangle then lands in a few cells and each cell holds a
    // few triangles.
    double meanSize = 0.0;
    for (const ContactTriangle& t : triangles)
    {
        meanSize += std::max({t.max.x - t.min.x, t.max.y - t.min.y, t.max.z - t.min.z});
    }
    meanSize /= static_cast<double>(triangles.size());
    const double cellSize = std::max({meanSize, extent / kMaxGridResolution, 4.0 * tolerance});
    const Vec3d origin = boundsMin - Vec3d{tolerance, tolerance, tolerance};
    const auto cellOf = [&](double value, double base)
    { return static_cast<std::uint64_t>(std::max(0.0, std::floor((value - base) / cellSize))); };
    const auto cellRange = [&](const Vec3d& min, const Vec3d& max)
    {
        return std::array<std::uint64_t, 6>{cellOf(min.x - tolerance, origin.x), cellOf(min.y - tolerance, origin.y),
                                            cellOf(min.z - tolerance, origin.z), cellOf(max.x + tolerance, origin.x),
                                            cellOf(max.y + tolerance, origin.y), cellOf(max.z + tolerance, origin.z)};
    };
    const auto key = [](std::uint64_t x, std::uint64_t y, std::uint64_t z)
    { return (x << (2 * kAxisBits)) | (y << kAxisBits) | z; };

    std::vector<std::pair<std::uint64_t, std::uint32_t>> entries;
    for (std::uint32_t t = 0; t < triangles.size(); ++t)
    {
        const auto r = cellRange(triangles[t].min, triangles[t].max);
        for (std::uint64_t x = r[0]; x <= r[3]; ++x)
        {
            for (std::uint64_t y = r[1]; y <= r[4]; ++y)
            {
                for (std::uint64_t z = r[2]; z <= r[5]; ++z)
                {
                    entries.emplace_back(key(x, y, z), t);
                }
            }
        }
    }
    std::ranges::sort(entries);

    std::map<std::pair<std::uint32_t, std::uint32_t>, Accumulator> pairs;
    for (std::size_t begin = 0; begin < entries.size();)
    {
        std::size_t end = begin + 1;
        while (end < entries.size() && entries[end].first == entries[begin].first)
        {
            ++end;
        }
        const std::uint64_t cell = entries[begin].first;
        for (std::size_t i = begin; i < end; ++i)
        {
            const ContactTriangle& a = triangles[entries[i].second];
            for (std::size_t j = i + 1; j < end; ++j)
            {
                const ContactTriangle& b = triangles[entries[j].second];
                if (a.mesh == b.mesh || detail::Dot(a.normal, b.normal) > -settings.normalTolerance)
                {
                    continue;
                }
                // Visit each pair once: in the cell holding the low corner of their boxes' overlap.
                const Vec3d low{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y), std::max(a.min.z, b.min.z)};
                const Vec3d high{std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y), std::min(a.max.z, b.max.z)};
                if (low.x > high.x + 2 * tolerance || low.y > high.y + 2 * tolerance || low.z > high.z + 2 * tolerance)
                {
                    continue;
                }
                if (key(cellOf(low.x - tolerance, origin.x), cellOf(low.y - tolerance, origin.y),
                        cellOf(low.z - tolerance, origin.z)) != cell)
                {
                    continue;
                }
                const bool coplanar =
                    std::ranges::all_of(b.corners, [&](const Vec3d& c)
                                        { return std::abs(detail::Dot(a.normal, c) - a.offset) <= tolerance; });
                if (!coplanar)
                {
                    continue;
                }
                const auto [area, centroid] = Overlap(a, b);
                if (area <= 0.0)
                {
                    continue;
                }
                const bool aIsLow = a.mesh < b.mesh;
                Accumulator& acc = pairs[{std::min(a.mesh, b.mesh), std::max(a.mesh, b.mesh)}];
                acc.area += area;
                acc.centroid = acc.centroid + centroid * area;
                acc.normal = acc.normal + (aIsLow ? a.normal : a.normal * -1.0) * area;
            }
        }
        begin = end;
    }

    std::vector<Contact> contacts;
    for (const auto& [pair, acc] : pairs)
    {
        if (acc.area < settings.minArea)
        {
            continue;
        }
        Contact contact;
        contact.a = pair.first;
        contact.b = pair.second;
        contact.area = acc.area;
        contact.centroid = (acc.centroid * (1.0 / acc.area)).ToFloat();
        contact.normal = (acc.normal * (1.0 / detail::Length(acc.normal))).ToFloat();
        contacts.push_back(contact);
    }
    return contacts;
}

BondGraph BuildBondGraph(std::span<const Chunk> chunks, std::span<const DestructionMaterial> materials,
                         const BondSettings& settings, std::span<const FractureMesh> anchorMeshes)
{
    std::vector<std::uint32_t> leaves;
    std::vector<const FractureMesh*> meshes;
    AABB bounds = AABB::Empty();
    for (std::uint32_t c = 0; c < chunks.size(); ++c)
    {
        if (chunks[c].IsLeaf())
        {
            leaves.push_back(c);
            meshes.push_back(&chunks[c].mesh);
            bounds.Merge(ComputeBounds(chunks[c].mesh));
        }
    }
    for (const FractureMesh& anchor : anchorMeshes)
    {
        meshes.push_back(&anchor);
    }
    FractureMesh ground;
    if (settings.groundHeight && bounds.IsValid())
    {
        const float h = *settings.groundHeight;
        ground = MakeBoxMesh({bounds.min.x - 1.0f, h - 1.0f, bounds.min.z - 1.0f},
                             {bounds.max.x + 1.0f, h, bounds.max.z + 1.0f});
        meshes.push_back(&ground);
    }

    BondGraph graph;
    for (const Contact& contact : FindContacts(meshes, settings.contact))
    {
        if (contact.b >= leaves.size())
        {
            if (contact.a < leaves.size())
            {
                graph.anchors.push_back(leaves[contact.a]);
            }
            continue; // anchor-anchor contact
        }
        const Chunk& a = chunks[leaves[contact.a]];
        const Chunk& b = chunks[leaves[contact.b]];
        INTERI_ASSERT(a.material < materials.size() && b.material < materials.size(), "chunk material out of range");
        const DestructionMaterial& ma = materials[a.material];
        const DestructionMaterial& mb = materials[b.material];
        const auto area = static_cast<float>(contact.area);
        graph.bonds.push_back({.chunkA = leaves[contact.a],
                               .chunkB = leaves[contact.b],
                               .area = area,
                               .centroid = contact.centroid,
                               .normal = contact.normal,
                               .compressiveLimit = area * std::min(ma.compressiveStrength, mb.compressiveStrength),
                               .tensileLimit = area * std::min(ma.tensileStrength, mb.tensileStrength),
                               .shearLimit = area * std::min(ma.shearStrength, mb.shearStrength)});
    }
    std::ranges::sort(graph.anchors);
    graph.anchors.erase(std::ranges::unique(graph.anchors).begin(), graph.anchors.end());
    return graph;
}

std::size_t CountLeafComponents(std::span<const Chunk> chunks, std::span<const Bond> bonds)
{
    std::vector<std::uint32_t> parent(chunks.size());
    std::ranges::iota(parent, 0u);
    for (const Bond& bond : bonds)
    {
        const std::uint32_t a = FindRoot(parent, bond.chunkA);
        const std::uint32_t b = FindRoot(parent, bond.chunkB);
        parent[std::max(a, b)] = std::min(a, b);
    }
    std::size_t components = 0;
    for (std::uint32_t c = 0; c < chunks.size(); ++c)
    {
        components += chunks[c].IsLeaf() && FindRoot(parent, c) == c ? 1 : 0;
    }
    return components;
}

} // namespace interi::destruction
