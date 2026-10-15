// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

/// An edge of the connectivity graph: two node indices.
using GraphEdge = std::pair<std::uint32_t, std::uint32_t>;

/// Connected components of the graph over `nodeCount` nodes linked by the edges whose `broken` flag is 0 (all when
/// `broken` is empty). labels[i] becomes the smallest node index of i's component, so the result does not depend on
/// the order unions happen in. Lock-free parallel union-find over edge chunks.
void LabelComponents(JobSystem& jobs, std::uint32_t nodeCount, std::span<const GraphEdge> edges,
                     std::span<const std::uint8_t> broken, std::span<std::uint32_t> labels);

/// A connected group of nodes.
struct Island
{
    std::vector<std::uint32_t> nodes; ///< Ascending.
    bool anchored = false;            ///< Contains an anchored node.
};

/// Groups the nodes for which `include[i]` is non-zero (all when empty) by label. Islands are ordered by their
/// smallest node. anchored[i] marks anchor nodes.
[[nodiscard]] std::vector<Island> GroupIslands(std::span<const std::uint32_t> labels,
                                               std::span<const std::uint8_t> anchored,
                                               std::span<const std::uint8_t> include = {});

} // namespace interi::destruction
