// SPDX-License-Identifier: MIT
#include <interi/core/Random.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/Voronoi.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include "Vec3d.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace interi::destruction
{
namespace
{

using detail::Vec3d;

constexpr std::uint32_t kAttemptsPerSeed = 64;
constexpr std::uint64_t kMaxGridCells = 1u << 20;

Vec3 RandomInBox(Random& random, const AABB& box)
{
    const float x = random.NextFloat(box.min.x, box.max.x);
    const float y = random.NextFloat(box.min.y, box.max.y);
    const float z = random.NextFloat(box.min.z, box.max.z);
    return {x, y, z};
}

/// Uniform direction by rejection sampling in the unit ball: no trigonometry, so it is identical everywhere.
Vec3 RandomDirection(Random& random)
{
    for (;;)
    {
        const float x = random.NextFloat(-1.0f, 1.0f);
        const float y = random.NextFloat(-1.0f, 1.0f);
        const float z = random.NextFloat(-1.0f, 1.0f);
        const float lengthSquared = x * x + y * y + z * z;
        if (lengthSquared <= 1.0f && lengthSquared > 1e-6f)
        {
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            return {x * inverse, y * inverse, z * inverse};
        }
    }
}

std::uint32_t CellCount(float extent, float cellSize)
{
    return std::max(1u, static_cast<std::uint32_t>(std::ceil(extent / cellSize)));
}

struct Cell
{
    std::vector<FractureMesh> components;
    std::vector<double> componentVolumes;
    std::vector<std::uint32_t> neighbors; ///< Seeds whose bisector cut this cell's convex region.
    double volume = 0.0;
};

class VoronoiBuilder
{
public:
    VoronoiBuilder(const FractureMesh& mesh, std::span<const Vec3> seeds)
        : m_mesh(mesh)
        , m_seeds(seeds)
        , m_tolerance(CutTolerance(mesh))
    {
        AABB bounds = ComputeBounds(mesh);
        const float margin = Length(bounds.max - bounds.min) * 0.01f + 1e-3f;
        bounds.min -= Vec3(margin);
        bounds.max += Vec3(margin);
        m_box = MakeBoxMesh(bounds.min, bounds.max);
        m_boxTolerance = CutTolerance(m_box);
    }

    /// Mesh cell of seed `i` among the `alive` seeds (ascending indices).
    [[nodiscard]] Cell Compute(std::uint32_t i, std::span<const std::uint32_t> alive) const
    {
        const Vec3d seed = Vec3d::From(m_seeds[i]);
        std::vector<std::pair<double, std::uint32_t>> candidates;
        candidates.reserve(alive.size());
        for (const std::uint32_t j : alive)
        {
            if (j != i)
            {
                const Vec3d delta = Vec3d::From(m_seeds[j]) - seed;
                candidates.emplace_back(detail::Dot(delta, delta), j);
            }
        }
        std::ranges::sort(candidates);

        // Convex Voronoi region clipped to the padded bounds; only bisectors that cut it matter for the mesh.
        Cell cell;
        std::vector<CutPlane> planes;
        FractureMesh region = m_box;
        double radius = MaxDistance(region, seed);
        for (const auto& [distanceSquared, j] : candidates)
        {
            if (std::sqrt(distanceSquared) * 0.5 > radius + m_boxTolerance)
            {
                break; // every further bisector is beyond the region
            }
            const Vec3d other = Vec3d::From(m_seeds[j]);
            const Vec3d normal = other - seed;
            const Vec3d midpoint = (seed + other) * 0.5;
            const CutPlane plane =
                CutPlane::FromPointNormal({midpoint.x, midpoint.y, midpoint.z}, {normal.x, normal.y, normal.z});
            if (!Crosses(region, plane, m_boxTolerance))
            {
                continue;
            }
            region = CutMesh(region, plane, m_boxTolerance);
            planes.push_back(plane);
            cell.neighbors.push_back(j);
            if (region.triangles.empty())
            {
                return cell; // the seed lies outside the bounds
            }
            radius = MaxDistance(region, seed);
        }

        FractureMesh piece = m_mesh;
        for (const CutPlane& plane : planes)
        {
            if (Crosses(piece, plane, m_tolerance))
            {
                piece = CutMesh(piece, plane, m_tolerance);
            }
            if (piece.triangles.empty())
            {
                return cell;
            }
        }
        cell.components = SplitConnectedComponents(piece);
        for (const FractureMesh& component : cell.components)
        {
            cell.componentVolumes.push_back(ComputeVolume(component));
            cell.volume += cell.componentVolumes.back();
        }
        return cell;
    }

private:
    static double MaxDistance(const FractureMesh& mesh, const Vec3d& point) noexcept
    {
        double maxSquared = 0.0;
        for (const Vec3& p : mesh.positions)
        {
            const Vec3d delta = Vec3d::From(p) - point;
            maxSquared = std::max(maxSquared, detail::Dot(delta, delta));
        }
        return std::sqrt(maxSquared);
    }

    /// True if some vertex is in front of the plane, i.e. cutting would remove something.
    static bool Crosses(const FractureMesh& mesh, const CutPlane& plane, double tolerance) noexcept
    {
        const double length = std::sqrt(plane.normal[0] * plane.normal[0] + plane.normal[1] * plane.normal[1] +
                                        plane.normal[2] * plane.normal[2]);
        return std::ranges::any_of(mesh.positions,
                                   [&](const Vec3& p) { return plane.Evaluate(p) > tolerance * length; });
    }

    const FractureMesh& m_mesh;
    std::span<const Vec3> m_seeds;
    FractureMesh m_box;
    double m_tolerance;
    double m_boxTolerance;
};

} // namespace

std::vector<Vec3> GenerateSeeds(const FractureMesh& mesh, const SeedSettings& settings)
{
    std::vector<Vec3> seeds;
    if (mesh.triangles.empty())
    {
        return seeds;
    }
    Random random(settings.randomSeed);
    const AABB bounds = ComputeBounds(mesh);

    switch (settings.pattern)
    {
    case SeedPattern::Uniform:
    case SeedPattern::Clustered:
    {
        std::uint64_t attempts = static_cast<std::uint64_t>(settings.count) * kAttemptsPerSeed;
        while (seeds.size() < settings.count && attempts-- > 0)
        {
            Vec3 point;
            if (settings.pattern == SeedPattern::Uniform)
            {
                point = RandomInBox(random, bounds);
            }
            else
            {
                // Uniform radius (not uniform volume) puts more points near the impact: density ~ 1 / r^2.
                const float radius = settings.clusterRadius * random.NextFloat01();
                point = settings.impactPoint + RandomDirection(random) * radius;
            }
            if (bounds.Contains(point) && ContainsPoint(mesh, point))
            {
                seeds.push_back(point);
            }
        }
        break;
    }
    case SeedPattern::Grid:
    {
        const Vec3 size = bounds.max - bounds.min;
        const std::uint32_t nx = CellCount(size.x, settings.gridCellSize.x);
        const std::uint32_t ny = CellCount(size.y, settings.gridCellSize.y);
        const std::uint32_t nz = CellCount(size.z, settings.gridCellSize.z);
        if (static_cast<std::uint64_t>(nx) * ny * nz > kMaxGridCells)
        {
            break;
        }
        // Cells are spread over the bounds so the outermost cells are centred on the mesh.
        const Vec3 cell(size.x / static_cast<float>(nx), size.y / static_cast<float>(ny),
                        size.z / static_cast<float>(nz));
        const float jitter = std::clamp(settings.jitter, 0.0f, 1.0f);
        for (std::uint32_t z = 0; z < nz; ++z)
        {
            for (std::uint32_t y = 0; y < ny; ++y)
            {
                for (std::uint32_t x = 0; x < nx; ++x)
                {
                    const Vec3 offset(random.NextFloat(-0.5f, 0.5f), random.NextFloat(-0.5f, 0.5f),
                                      random.NextFloat(-0.5f, 0.5f));
                    const Vec3 point(bounds.min.x + cell.x * (static_cast<float>(x) + 0.5f + offset.x * jitter),
                                     bounds.min.y + cell.y * (static_cast<float>(y) + 0.5f + offset.y * jitter),
                                     bounds.min.z + cell.z * (static_cast<float>(z) + 0.5f + offset.z * jitter));
                    if (ContainsPoint(mesh, point))
                    {
                        seeds.push_back(point);
                    }
                }
            }
        }
        break;
    }
    }
    return seeds;
}

FractureResult VoronoiFracture(const FractureMesh& mesh, std::span<const Vec3> seeds, const VoronoiSettings& settings,
                               JobSystem& jobs)
{
    FractureResult result;
    if (seeds.empty() || mesh.triangles.empty())
    {
        return result;
    }

    // Unique seeds, lowest index kept.
    std::vector<std::uint32_t> order(seeds.size());
    for (std::uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    const auto key = [&](std::uint32_t i) { return std::tuple(seeds[i].x, seeds[i].y, seeds[i].z, i); };
    std::ranges::sort(order, [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });
    std::vector<bool> alive(seeds.size(), false);
    for (std::size_t k = 0; k < order.size(); ++k)
    {
        alive[order[k]] = k == 0 || seeds[order[k]] != seeds[order[k - 1]];
    }

    const VoronoiBuilder builder(mesh, seeds);
    std::vector<Cell> cells(seeds.size());
    std::vector<std::uint32_t> dirty;
    for (std::uint32_t i = 0; i < seeds.size(); ++i)
    {
        if (alive[i])
        {
            dirty.push_back(i);
        }
    }

    for (;;)
    {
        std::vector<std::uint32_t> aliveList;
        for (std::uint32_t i = 0; i < seeds.size(); ++i)
        {
            if (alive[i])
            {
                aliveList.push_back(i);
            }
        }
        ParallelFor(jobs, 0, dirty.size(), 1,
                    [&](std::size_t k) { cells[dirty[k]] = builder.Compute(dirty[k], aliveList); });

        // Empty cells contribute nothing and change no other cell; slivers are merged by dropping their seed.
        std::vector<std::uint32_t> slivers;
        std::uint32_t largest = aliveList.front();
        std::size_t nonEmpty = 0;
        for (const std::uint32_t i : aliveList)
        {
            if (cells[i].volume > cells[largest].volume)
            {
                largest = i;
            }
            if (cells[i].components.empty())
            {
                alive[i] = false;
                continue;
            }
            ++nonEmpty;
            if (cells[i].volume < settings.minPieceVolume)
            {
                slivers.push_back(i);
            }
        }
        if (slivers.size() == nonEmpty)
        {
            std::erase(slivers, largest); // the whole mesh is below the threshold: keep one piece
        }
        if (slivers.empty())
        {
            break;
        }

        for (const std::uint32_t i : slivers)
        {
            alive[i] = false;
            ++result.mergedSeeds;
        }
        dirty.clear();
        for (const std::uint32_t i : slivers)
        {
            for (const std::uint32_t j : cells[i].neighbors)
            {
                if (alive[j])
                {
                    dirty.push_back(j);
                }
            }
            cells[i] = {};
        }
        std::ranges::sort(dirty);
        dirty.erase(std::ranges::unique(dirty).begin(), dirty.end());
    }

    for (std::uint32_t i = 0; i < seeds.size(); ++i)
    {
        Cell& cell = cells[i];
        if (!alive[i])
        {
            continue;
        }
        const auto largestComponent =
            static_cast<std::size_t>(std::ranges::max_element(cell.componentVolumes) - cell.componentVolumes.begin());
        for (std::size_t c = 0; c < cell.components.size(); ++c)
        {
            if (c != largestComponent && cell.componentVolumes[c] < settings.minPieceVolume)
            {
                result.discardedVolume += cell.componentVolumes[c];
                continue;
            }
            result.pieces.push_back(
                {.mesh = std::move(cell.components[c]), .seed = i, .volume = cell.componentVolumes[c]});
        }
    }
    return result;
}

} // namespace interi::destruction
