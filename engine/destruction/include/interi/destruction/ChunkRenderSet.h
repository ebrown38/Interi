// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/DestructibleAsset.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

/// Picks the chunk meshes that draw a set of leaf chunks moving together (one piece, or one debris body).
///
/// Every cooked chunk has a render mesh, and an inner chunk's mesh is the surface of its leaves without the faces
/// between them. A set of leaves is therefore drawn with the fewest meshes by covering it with the highest chunks
/// whose leaves all belong to the set: an intact object draws its root chunk, and only the regions that broke apart
/// fall back to their smaller chunks. (Fracture discards slivers, so a parent can be marginally larger than the
/// union of its leaves; the switch is not visible at that scale.)
///
/// Not thread-safe: Cover uses scratch storage. Use one set per thread.
class ChunkRenderSet
{
public:
    /// `chunks` are breadth-first per object (every parent before its children), as cooked.
    explicit ChunkRenderSet(std::span<const AssetChunk> chunks);

    /// Appends the chunks covering `leaves` (distinct leaf chunk indices, any order) to `out`, in increasing order.
    void Cover(std::span<const std::uint32_t> leaves, std::vector<std::uint32_t>& out);

    [[nodiscard]] std::uint32_t LeafCount(std::uint32_t chunk) const noexcept { return m_leafCount[chunk]; }

private:
    std::vector<std::uint32_t> m_parent;
    std::vector<std::uint32_t> m_leafCount; ///< Leaves under each chunk (1 for a leaf).
    std::vector<std::uint32_t> m_covered;   ///< Scratch: leaves of the current set under each chunk.
    std::vector<std::uint32_t> m_touched;   ///< Scratch: chunks with m_covered > 0.
};

} // namespace interi::destruction
