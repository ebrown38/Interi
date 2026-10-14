// SPDX-License-Identifier: MIT
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using interi::ChunkView;
using interi::Entity;
using interi::Query;
using interi::World;

namespace
{

struct QPosition
{
    float x = 0;
};

struct QHealth
{
    int value = 0;
};

struct QFrozen
{
};

std::size_t CountRows(World& world, Query& query)
{
    std::size_t rows = 0;
    query.ForEachChunk(world, [&](ChunkView& view) { rows += view.Count(); });
    return rows;
}

} // namespace

TEST(Query, WithWithout_FiltersArchetypes)
{
    World world;
    for (int i = 0; i < 10; ++i)
    {
        (void)world.CreateEntity(QPosition{}, QHealth{});
        (void)world.CreateEntity(QPosition{});
        (void)world.CreateEntity(QPosition{}, QFrozen{});
    }
    Query positions;
    positions.With<QPosition>();
    EXPECT_EQ(CountRows(world, positions), 30u);

    Query healthy;
    healthy.With<QPosition, QHealth>();
    EXPECT_EQ(CountRows(world, healthy), 10u);

    Query thawed;
    thawed.With<QPosition>().Without<QFrozen>();
    EXPECT_EQ(CountRows(world, thawed), 20u);
}

TEST(Query, ReadWrite_AccessColumns)
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 100; ++i)
    {
        entities.push_back(world.CreateEntity(QPosition{static_cast<float>(i)}, QHealth{i}));
    }
    Query query;
    query.With<QPosition, QHealth>();
    query.ForEachChunk(world,
                       [](ChunkView& view)
                       {
                           const auto health = view.Read<QHealth>();
                           auto position = view.Write<QPosition>();
                           for (std::uint32_t row = 0; row < view.Count(); ++row)
                           {
                               position[row].x += static_cast<float>(health[row].value);
                           }
                       });
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(world.Get<QPosition>(entities[static_cast<std::size_t>(i)])->x, 2.0f * static_cast<float>(i));
    }
}

TEST(Query, Changed_OnlyModifiedSinceLastRun)
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 3000; ++i) // several chunks
    {
        entities.push_back(world.CreateEntity(QPosition{}, QHealth{i}));
    }
    const std::size_t chunks = world.ChunkCount(world.ArchetypeOf(entities[0]));
    ASSERT_GT(chunks, 2u);

    Query changed;
    changed.Changed<QPosition>();
    const auto chunksVisited = [&]
    {
        std::size_t visited = 0;
        changed.ForEachChunk(world, [&](ChunkView&) { ++visited; });
        return visited;
    };

    EXPECT_EQ(chunksVisited(), chunks); // first run: everything is new
    EXPECT_EQ(chunksVisited(), 0u);     // nothing changed since

    *world.GetMut<QPosition>(entities[1500]) = QPosition{5};
    std::vector<Entity> seen;
    changed.ForEachChunk(world,
                         [&](ChunkView& view)
                         {
                             const auto rows = view.Entities();
                             seen.assign(rows.begin(), rows.end());
                         });
    EXPECT_NE(std::ranges::find(seen, entities[1500]), seen.end());
    EXPECT_LT(seen.size(), entities.size());
    EXPECT_EQ(chunksVisited(), 0u);

    // Reading Position through another query, or writing another component, is not a change of Position.
    Query other;
    other.With<QPosition, QHealth>();
    other.ForEachChunk(world,
                       [](ChunkView& view)
                       {
                           (void)view.Read<QPosition>();
                           (void)view.Write<QHealth>();
                       });
    EXPECT_EQ(chunksVisited(), 0u);

    // Writes through another query are seen; the query's own writes are not seen by itself next time.
    other.ForEachChunk(world, [](ChunkView& view) { (void)view.Write<QPosition>(); });
    std::size_t visited = 0;
    changed.ForEachChunk(world,
                         [&](ChunkView& view)
                         {
                             ++visited;
                             (void)view.Write<QPosition>();
                         });
    EXPECT_EQ(visited, chunks);
    EXPECT_EQ(chunksVisited(), 0u);
}

TEST(Query, Changed_SurvivesSwapRemoveIntoOlderChunk)
{
    World world;
    std::vector<Entity> entities;
    for (int i = 0; i < 3000; ++i)
    {
        entities.push_back(world.CreateEntity(QPosition{}, QHealth{i}));
    }
    Query changed;
    changed.Changed<QPosition>();
    changed.ForEachChunk(world, [](ChunkView&) {});

    // Change the last entity, then destroy one in the first chunk: the last row moves into the old chunk.
    *world.GetMut<QPosition>(entities.back()) = QPosition{9};
    ASSERT_TRUE(world.DestroyEntity(entities[0]));
    bool found = false;
    changed.ForEachChunk(
        world, [&](ChunkView& view)
        { found = found || std::ranges::find(view.Entities(), entities.back()) != view.Entities().end(); });
    EXPECT_TRUE(found);
}
