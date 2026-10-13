// SPDX-License-Identifier: MIT
#pragma once

#include <interi/ecs/World.h>
#include <interi/testing/StateHasher.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace interi::testing
{

struct CbPosition
{
    float x = 0;
};

struct CbHealth
{
    int value = 0;
};

struct CbVelocity
{
    float x = 0;
};

/// Hashes the observable world state: archetypes in creation order, each row's entity handle and the bytes of its
/// (trivially copyable) components in column layout order. Use only with trivially copyable component types.
inline std::uint64_t HashWorld(const World& world)
{
    StateHasher hasher;
    for (const auto& archetype : world.Archetypes())
    {
        hasher.Add(archetype->columns.size());
        for (const detail::Chunk& chunk : archetype->chunks)
        {
            for (std::uint32_t row = 0; row < chunk.count; ++row)
            {
                const Entity entity = archetype->Entities(chunk)[row];
                hasher.Add(entity.Index());
                hasher.Add(entity.Generation());
                for (std::size_t column = 0; column < archetype->columns.size(); ++column)
                {
                    const std::size_t size = archetype->columns[column].size;
                    hasher.AddBytes(std::span<const std::byte>(
                        static_cast<const std::byte*>(archetype->At(chunk, column, row)), size));
                }
            }
        }
    }
    return hasher.Finish();
}

} // namespace interi::testing
