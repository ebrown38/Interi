// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>

#include <xxhash.h>

namespace interi
{

std::uint64_t HashBytes(const void* data, std::size_t size, std::uint64_t seed) noexcept
{
    return XXH3_64bits_withSeed(data, size, seed);
}

} // namespace interi
