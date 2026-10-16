// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/ChunkRenderSet.h>

#include <algorithm>

namespace interi::destruction
{

ChunkRenderSet::ChunkRenderSet(std::span<const AssetChunk> chunks)
    : m_parent(chunks.size())
    , m_leafCount(chunks.size(), 0)
    , m_covered(chunks.size(), 0)
{
    for (std::size_t c = chunks.size(); c-- > 0;)
    {
        m_parent[c] = chunks[c].parent;
        if (chunks[c].childCount == 0)
        {
            m_leafCount[c] = 1;
        }
        if (chunks[c].parent != kNoChunk)
        {
            INTERI_ASSERT(chunks[c].parent < c, "chunks must be breadth-first");
            m_leafCount[chunks[c].parent] += m_leafCount[c];
        }
    }
}

void ChunkRenderSet::Cover(std::span<const std::uint32_t> leaves, std::vector<std::uint32_t>& out)
{
    for (const std::uint32_t leaf : leaves)
    {
        INTERI_ASSERT(leaf < m_parent.size() && m_leafCount[leaf] == 1, "not a leaf chunk");
        for (std::uint32_t c = leaf; c != kNoChunk; c = m_parent[c])
        {
            if (m_covered[c]++ == 0)
            {
                m_touched.push_back(c);
            }
        }
    }
    const std::size_t first = out.size();
    for (const std::uint32_t c : m_touched)
    {
        const std::uint32_t parent = m_parent[c];
        if (m_covered[c] == m_leafCount[c] && (parent == kNoChunk || m_covered[parent] != m_leafCount[parent]))
        {
            out.push_back(c);
        }
    }
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end());
    for (const std::uint32_t c : m_touched)
    {
        m_covered[c] = 0;
    }
    m_touched.clear();
}

} // namespace interi::destruction
