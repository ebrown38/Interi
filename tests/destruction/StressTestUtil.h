// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/StressSolver.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace interi::destruction::testing
{

using Cell = std::array<int, 3>;

/// A structure of unit cubes: cell (x, y, z) has its centroid at (x + 0.5, y + 0.5, z + 0.5). Node i is cells[i];
/// face-adjacent cells are bonded (area 1, centroid on the shared face, lower node index first).
struct GridStructure
{
    std::vector<Cell> cells;
    StressGraph graph;

    [[nodiscard]] std::optional<std::uint32_t> Node(const Cell& cell) const
    {
        for (std::uint32_t i = 0; i < cells.size(); ++i)
        {
            if (cells[i] == cell)
            {
                return i;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint32_t> Bond(const Cell& a, const Cell& b) const
    {
        const auto nodeA = Node(a);
        const auto nodeB = Node(b);
        if (!nodeA || !nodeB)
        {
            return std::nullopt;
        }
        for (std::uint32_t i = 0; i < graph.bonds.size(); ++i)
        {
            const StressBond& bond = graph.bonds[i];
            if ((bond.nodeA == *nodeA && bond.nodeB == *nodeB) || (bond.nodeA == *nodeB && bond.nodeB == *nodeA))
            {
                return i;
            }
        }
        return std::nullopt;
    }
};

struct GridOptions
{
    float mass = 1000.0f;
    float limit = 1.0e12f; ///< Every bond limit (N).
    std::function<bool(const Cell&)> anchored = [](const Cell& cell) { return cell[1] == 0; };
};

inline GridStructure MakeGrid(std::vector<Cell> cells, const GridOptions& options = {})
{
    GridStructure grid;
    grid.cells = std::move(cells);
    for (const Cell& cell : grid.cells)
    {
        grid.graph.nodes.push_back({.position = {static_cast<float>(cell[0]) + 0.5f, static_cast<float>(cell[1]) + 0.5f,
                                                 static_cast<float>(cell[2]) + 0.5f},
                                    .mass = options.mass,
                                    .anchored = options.anchored(cell)});
    }
    for (std::uint32_t a = 0; a < grid.cells.size(); ++a)
    {
        for (std::uint32_t b = a + 1; b < grid.cells.size(); ++b)
        {
            const Cell& ca = grid.cells[a];
            const Cell& cb = grid.cells[b];
            const int dx = cb[0] - ca[0];
            const int dy = cb[1] - ca[1];
            const int dz = cb[2] - ca[2];
            if (std::abs(dx) + std::abs(dy) + std::abs(dz) != 1)
            {
                continue;
            }
            const Vec3 normal(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
            const Vec3 centroid = grid.graph.nodes[a].position + normal * 0.5f;
            grid.graph.bonds.push_back({.nodeA = a,
                                        .nodeB = b,
                                        .centroid = centroid,
                                        .normal = normal,
                                        .area = 1.0f,
                                        .compressiveLimit = options.limit,
                                        .tensileLimit = options.limit,
                                        .shearLimit = options.limit});
        }
    }
    return grid;
}

/// Every cell of the box [0, sx) x [0, sy) x [0, sz), x fastest.
inline std::vector<Cell> BoxCells(int sx, int sy, int sz)
{
    std::vector<Cell> cells;
    for (int z = 0; z < sz; ++z)
    {
        for (int y = 0; y < sy; ++y)
        {
            for (int x = 0; x < sx; ++x)
            {
                cells.push_back({x, y, z});
            }
        }
    }
    return cells;
}

} // namespace interi::destruction::testing
