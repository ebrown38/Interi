// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/Islands.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <atomic>
#include <memory>
#include <utility>

namespace interi::destruction
{

namespace
{

constexpr std::size_t kEdgeGrain = 2048;
constexpr std::size_t kNodeGrain = 4096;

/// Root of `node` with path halving. Concurrent halving only ever points a node at one of its ancestors, so it is
/// safe alongside unions (which only re-point roots).
std::uint32_t FindRoot(std::atomic<std::uint32_t>* parent, std::uint32_t node) noexcept
{
    std::uint32_t p = parent[node].load(std::memory_order_acquire);
    while (p != node)
    {
        const std::uint32_t grand = parent[p].load(std::memory_order_acquire);
        if (grand != p)
        {
            std::uint32_t expected = p;
            parent[node].compare_exchange_weak(expected, grand, std::memory_order_acq_rel, std::memory_order_relaxed);
        }
        node = p;
        p = grand;
    }
    return node;
}

/// Links the two sets, always hanging the larger root under the smaller, so every root is its set's minimum.
void Unite(std::atomic<std::uint32_t>* parent, std::uint32_t a, std::uint32_t b) noexcept
{
    for (;;)
    {
        a = FindRoot(parent, a);
        b = FindRoot(parent, b);
        if (a == b)
        {
            return;
        }
        if (a > b)
        {
            std::swap(a, b);
        }
        std::uint32_t expected = b;
        if (parent[b].compare_exchange_strong(expected, a, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }
    }
}

} // namespace

void LabelComponents(JobSystem& jobs, std::uint32_t nodeCount, std::span<const GraphEdge> edges,
                     std::span<const std::uint8_t> broken, std::span<std::uint32_t> labels)
{
    INTERI_ASSERT(labels.size() == nodeCount);
    INTERI_ASSERT(broken.empty() || broken.size() == edges.size());
    const std::unique_ptr<std::atomic<std::uint32_t>[]> parent(new std::atomic<std::uint32_t>[nodeCount]);
    ParallelFor(jobs, 0, nodeCount, kNodeGrain,
                [&](std::size_t i) { parent[i].store(static_cast<std::uint32_t>(i), std::memory_order_relaxed); });
    ParallelFor(jobs, 0, edges.size(), kEdgeGrain,
                [&](std::size_t e)
                {
                    if (broken.empty() || broken[e] == 0)
                    {
                        INTERI_ASSERT(edges[e].first < nodeCount && edges[e].second < nodeCount);
                        Unite(parent.get(), edges[e].first, edges[e].second);
                    }
                });
    ParallelFor(jobs, 0, nodeCount, kNodeGrain,
                [&](std::size_t i) { labels[i] = FindRoot(parent.get(), static_cast<std::uint32_t>(i)); });
}

std::vector<Island> GroupIslands(std::span<const std::uint32_t> labels, std::span<const std::uint8_t> anchored,
                                 std::span<const std::uint8_t> include)
{
    INTERI_ASSERT(anchored.size() == labels.size());
    INTERI_ASSERT(include.empty() || include.size() == labels.size());
    // Labels are node indices, so islands come out ordered by label when created in label order.
    std::vector<std::uint32_t> islandOf(labels.size(), UINT32_MAX);
    std::vector<Island> islands;
    for (std::uint32_t node = 0; node < labels.size(); ++node)
    {
        if (!include.empty() && include[node] == 0)
        {
            continue;
        }
        std::uint32_t& index = islandOf[labels[node]];
        if (index == UINT32_MAX)
        {
            index = static_cast<std::uint32_t>(islands.size());
            islands.emplace_back();
        }
        Island& island = islands[index];
        island.nodes.push_back(node);
        island.anchored = island.anchored || anchored[node] != 0;
    }
    return islands;
}

} // namespace interi::destruction
