// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/DestructibleAsset.h>

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

struct GridAssetOptions
{
    float mass = 1000.0f;          ///< Per chunk (1 m^3 cubes).
    float fractureEnergy = 100.0f; ///< J/m^2 (bond health = this x 1 m^2).
    float limit = 1.0e12f;         ///< Every bond force limit (N).
    std::function<bool(const Cell&)> anchored = [](const Cell& cell) { return cell[1] == 0; };
};

/// An asset of unit-cube leaf chunks (no hierarchy): cell (x, y, z) spans [x, x+1] x [y, y+1] x [z, z+1] and is
/// chunk i = its index in `cells`. Face-adjacent cells are bonded.
inline DestructibleAsset MakeGridAsset(const std::vector<Cell>& cells, const GridAssetOptions& options = {})
{
    DestructibleAsset asset;
    DestructionMaterial material;
    material.name = "test";
    material.fractureEnergy = options.fractureEnergy;
    asset.materials.push_back(material);
    for (std::uint32_t i = 0; i < cells.size(); ++i)
    {
        const Vec3 corner(static_cast<float>(cells[i][0]), static_cast<float>(cells[i][1]),
                          static_cast<float>(cells[i][2]));
        AssetChunk chunk;
        chunk.volume = 1.0f;
        chunk.mass = options.mass;
        chunk.centroid = corner + Vec3(0.5f);
        CollisionHull hull;
        for (int c = 0; c < 8; ++c)
        {
            hull.points.push_back(corner + Vec3(static_cast<float>(c & 1), static_cast<float>((c >> 1) & 1),
                                                static_cast<float>((c >> 2) & 1)));
        }
        chunk.hulls.push_back(hull);
        asset.chunks.push_back(chunk);
        if (options.anchored(cells[i]))
        {
            asset.anchors.push_back(i);
        }
    }
    for (std::uint32_t a = 0; a < cells.size(); ++a)
    {
        for (std::uint32_t b = a + 1; b < cells.size(); ++b)
        {
            const int dx = cells[b][0] - cells[a][0];
            const int dy = cells[b][1] - cells[a][1];
            const int dz = cells[b][2] - cells[a][2];
            if (std::abs(dx) + std::abs(dy) + std::abs(dz) != 1)
            {
                continue;
            }
            Bond bond;
            bond.chunkA = a;
            bond.chunkB = b;
            bond.area = 1.0f;
            bond.normal = Vec3(static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz));
            bond.centroid = asset.chunks[a].centroid + bond.normal * 0.5f;
            bond.compressiveLimit = options.limit;
            bond.tensileLimit = options.limit;
            bond.shearLimit = options.limit;
            asset.bonds.push_back(bond);
        }
    }
    return asset;
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

/// Index of the bond between two chunks.
inline std::optional<std::uint32_t> FindBond(const DestructibleAsset& asset, std::uint32_t a, std::uint32_t b)
{
    if (a > b)
    {
        std::swap(a, b);
    }
    for (std::uint32_t i = 0; i < asset.bonds.size(); ++i)
    {
        if (asset.bonds[i].chunkA == a && asset.bonds[i].chunkB == b)
        {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace interi::destruction::testing
