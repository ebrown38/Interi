// SPDX-License-Identifier: MIT
#include "Triangulate.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace interi::destruction::detail
{
namespace
{

using Loop = std::vector<std::uint32_t>;

double Orient(const Point2& a, const Point2& b, const Point2& c) noexcept
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

double SignedArea(std::span<const Point2> points, const Loop& loop) noexcept
{
    double area = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i)
    {
        const Point2& a = points[loop[i]];
        const Point2& b = points[loop[(i + 1) % loop.size()]];
        area += a.x * b.y - b.x * a.y;
    }
    return 0.5 * area;
}

/// Crossing-number test; points on the boundary may go either way.
bool InsideLoop(std::span<const Point2> points, const Loop& loop, const Point2& q) noexcept
{
    bool inside = false;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++)
    {
        const Point2& a = points[loop[i]];
        const Point2& b = points[loop[j]];
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x)
        {
            inside = !inside;
        }
    }
    return inside;
}

bool SamePoint(const Point2& a, const Point2& b) noexcept
{
    return a.x == b.x && a.y == b.y;
}

bool InTriangle(const Point2& a, const Point2& b, const Point2& c, const Point2& q, double epsilon) noexcept
{
    return Orient(a, b, q) >= -epsilon && Orient(b, c, q) >= -epsilon && Orient(c, a, q) >= -epsilon;
}

/// Splices `hole` (clockwise) into `polygon` (counter-clockwise) through a bridge from the hole's rightmost vertex
/// to a visible polygon vertex (Eberly, "Triangulation by Ear Clipping").
void BridgeHole(std::span<const Point2> points, Loop& polygon, const Loop& hole)
{
    std::size_t mIndex = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
    {
        if (points[hole[i]].x > points[hole[mIndex]].x)
        {
            mIndex = i;
        }
    }
    const Point2 m = points[hole[mIndex]];

    // Nearest polygon edge hit by the ray m + t * (1, 0).
    double bestX = std::numeric_limits<double>::infinity();
    std::size_t bestVertex = polygon.size();
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        const Point2& a = points[polygon[i]];
        const Point2& b = points[polygon[(i + 1) % polygon.size()]];
        if ((a.y - m.y) * (b.y - m.y) > 0.0 || a.y == b.y)
        {
            continue;
        }
        const double x = a.x + (m.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x < m.x || x >= bestX)
        {
            continue;
        }
        bestX = x;
        bestVertex = a.x > b.x ? i : (i + 1) % polygon.size();
        if (a.y == m.y && a.x >= m.x)
        {
            bestVertex = i;
        }
        else if (b.y == m.y && b.x >= m.x)
        {
            bestVertex = (i + 1) % polygon.size();
        }
    }
    if (bestVertex == polygon.size())
    {
        return; // not inside after all: leave the hole out
    }

    // A polygon vertex inside triangle (m, hit, candidate) would block the bridge: take the one closest in angle.
    const Point2 hit{bestX, m.y};
    const Point2 candidate = points[polygon[bestVertex]];
    if (!SamePoint(candidate, hit))
    {
        double bestSlope = std::numeric_limits<double>::infinity();
        double bestDistance = std::numeric_limits<double>::infinity();
        const bool upper = candidate.y > m.y;
        for (std::size_t i = 0; i < polygon.size(); ++i)
        {
            const Point2& q = points[polygon[i]];
            if (i == bestVertex || q.x <= m.x)
            {
                continue;
            }
            const bool inside = upper ? InTriangle(m, hit, candidate, q, 0.0) : InTriangle(m, candidate, hit, q, 0.0);
            if (!inside)
            {
                continue;
            }
            const double dx = q.x - m.x;
            const double slope = std::abs(q.y - m.y) / dx;
            const double distance = dx * dx + (q.y - m.y) * (q.y - m.y);
            if (slope < bestSlope || (slope == bestSlope && distance < bestDistance))
            {
                bestSlope = slope;
                bestDistance = distance;
                bestVertex = i;
            }
        }
    }

    Loop merged;
    merged.reserve(polygon.size() + hole.size() + 2);
    merged.insert(merged.end(), polygon.begin(), polygon.begin() + static_cast<std::ptrdiff_t>(bestVertex) + 1);
    for (std::size_t k = 0; k <= hole.size(); ++k)
    {
        merged.push_back(hole[(mIndex + k) % hole.size()]);
    }
    merged.push_back(polygon[bestVertex]);
    merged.insert(merged.end(), polygon.begin() + static_cast<std::ptrdiff_t>(bestVertex) + 1, polygon.end());
    polygon = std::move(merged);
}

void EarClip(std::span<const Point2> points, const Loop& polygon, double epsilon,
             std::vector<std::array<std::uint32_t, 3>>& out)
{
    const std::size_t n = polygon.size();
    if (n < 3)
    {
        return;
    }
    std::vector<std::size_t> prev(n);
    std::vector<std::size_t> next(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        prev[i] = (i + n - 1) % n;
        next[i] = (i + 1) % n;
    }

    const auto isEar = [&](std::size_t v, bool checkContainment)
    {
        const Point2& a = points[polygon[prev[v]]];
        const Point2& b = points[polygon[v]];
        const Point2& c = points[polygon[next[v]]];
        if (Orient(a, b, c) <= epsilon)
        {
            return false;
        }
        if (!checkContainment)
        {
            return true;
        }
        for (std::size_t k = next[next[v]]; k != prev[v]; k = next[k])
        {
            const Point2& q = points[polygon[k]];
            if (SamePoint(q, a) || SamePoint(q, b) || SamePoint(q, c))
            {
                continue;
            }
            if (InTriangle(a, b, c, q, epsilon))
            {
                return false;
            }
        }
        return true;
    };

    std::size_t remaining = n;
    std::size_t current = 0;
    while (remaining > 3)
    {
        std::size_t ear = n;
        for (int pass = 0; pass < 2 && ear == n; ++pass)
        {
            std::size_t v = current;
            for (std::size_t count = 0; count < remaining; ++count, v = next[v])
            {
                if (isEar(v, pass == 0))
                {
                    ear = v;
                    break;
                }
            }
        }
        if (ear == n)
        {
            ear = current; // no proper ear left (degenerate input): clip anyway so every vertex is used
        }
        out.push_back({polygon[prev[ear]], polygon[ear], polygon[next[ear]]});
        next[prev[ear]] = next[ear];
        prev[next[ear]] = prev[ear];
        current = prev[ear];
        --remaining;
    }
    out.push_back({polygon[prev[current]], polygon[current], polygon[next[current]]});
}

} // namespace

std::vector<std::array<std::uint32_t, 3>> TriangulateLoops(std::span<const Point2> points,
                                                           const std::vector<std::vector<std::uint32_t>>& loops)
{
    double minX = std::numeric_limits<double>::infinity();
    double minY = minX;
    double maxX = -minX;
    double maxY = -minX;
    for (const Point2& p : points)
    {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    const double extent = std::max(maxX - minX, maxY - minY);
    const double epsilon = extent * extent * 1e-12; // twice-area below which three points count as collinear

    struct Outer
    {
        Loop loop;
        double area;
        std::vector<std::size_t> holes;
    };
    std::vector<Outer> outers;
    std::vector<std::size_t> holeLoops;
    for (std::size_t i = 0; i < loops.size(); ++i)
    {
        if (loops[i].size() < 3)
        {
            continue;
        }
        const double area = SignedArea(points, loops[i]);
        if (area > epsilon)
        {
            outers.push_back({.loop = loops[i], .area = area, .holes = {}});
        }
        else if (area < -epsilon)
        {
            holeLoops.push_back(i);
        }
    }

    for (const std::size_t hole : holeLoops)
    {
        // Test a hole vertex that is not shared with an outer boundary (holes may touch it at a vertex).
        std::size_t owner = outers.size();
        for (std::size_t o = 0; o < outers.size(); ++o)
        {
            const bool smaller = owner == outers.size() || outers[o].area < outers[owner].area;
            if (smaller && InsideLoop(points, outers[o].loop, points[loops[hole][0]]))
            {
                owner = o;
            }
        }
        if (owner != outers.size())
        {
            outers[owner].holes.push_back(hole);
        }
    }

    std::vector<std::array<std::uint32_t, 3>> triangles;
    for (Outer& outer : outers)
    {
        // Rightmost holes first, so later bridges cannot cross earlier ones.
        std::ranges::sort(outer.holes,
                          [&](std::size_t a, std::size_t b)
                          {
                              const auto maxX = [&](std::size_t h)
                              {
                                  double x = -std::numeric_limits<double>::infinity();
                                  for (const std::uint32_t v : loops[h])
                                  {
                                      x = std::max(x, points[v].x);
                                  }
                                  return x;
                              };
                              const double xa = maxX(a);
                              const double xb = maxX(b);
                              return xa != xb ? xa > xb : a < b;
                          });
        for (const std::size_t hole : outer.holes)
        {
            BridgeHole(points, outer.loop, loops[hole]);
        }
        EarClip(points, outer.loop, epsilon, triangles);
    }
    return triangles;
}

} // namespace interi::destruction::detail
