// SPDX-License-Identifier: MIT
#include <interi/core/memory/Allocator.h>
#include <interi/ecs/World.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using interi::Entity;
using interi::World;

namespace
{

struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float x = 0, y = 0, z = 0;
};

struct Health
{
    int value = 100;
};

struct StaticTag
{
};

/// Counts live instances, to prove every constructed component is destroyed exactly once.
struct Tracked
{
    static inline int s_live = 0;

    explicit Tracked(int v)
        : value(std::make_unique<int>(v))
    {
        ++s_live;
    }
    Tracked(Tracked&& other) noexcept
        : value(std::move(other.value))
    {
        ++s_live;
    }
    Tracked& operator=(Tracked&& other) noexcept = default;
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { --s_live; }

    std::unique_ptr<int> value;
};

class CountingAllocator final : public interi::IAllocator
{
public:
    void* Allocate(std::size_t size, std::size_t alignment) noexcept override
    {
        liveBytes += size;
        return interi::GetDefaultAllocator().Allocate(size, alignment);
    }
    void Free(void* ptr, std::size_t size) noexcept override
    {
        if (ptr != nullptr)
        {
            liveBytes -= size;
        }
        interi::GetDefaultAllocator().Free(ptr, size);
    }

    std::size_t liveBytes = 0;
};

struct Big
{
    std::array<std::byte, 40000> bytes{};
};

} // namespace

TEST(ComponentRegistry, SameType_SameId_DistinctTypes_DistinctIds)
{
    EXPECT_EQ(interi::ComponentId<Position>(), interi::ComponentId<Position>());
    EXPECT_NE(interi::ComponentId<Position>(), interi::ComponentId<Velocity>());
    const auto& info = interi::ComponentInfoOf(interi::ComponentId<Position>());
    EXPECT_EQ(info.size, sizeof(Position));
    EXPECT_EQ(info.alignment, alignof(Position));
    EXPECT_NE(info.name.find("Position"), std::string::npos);
    EXPECT_EQ(interi::ComponentInfoOf(interi::ComponentId<StaticTag>()).size, 0u);
}

TEST(World, CreateEntity_IsAliveWithNoComponents)
{
    World world;
    const Entity e = world.CreateEntity();
    EXPECT_TRUE(world.IsAlive(e));
    EXPECT_FALSE(world.Has<Position>(e));
    EXPECT_EQ(world.EntityCount(), 1u);
}

TEST(World, CreateWithComponents_ValuesReadable)
{
    World world;
    const Entity e = world.CreateEntity(Position{1, 2, 3}, Health{7});
    ASSERT_TRUE(world.Has<Position>(e));
    ASSERT_TRUE(world.Has<Health>(e));
    EXPECT_FALSE(world.Has<Velocity>(e));
    EXPECT_EQ(world.Get<Position>(e)->y, 2.0f);
    EXPECT_EQ(world.Get<Health>(e)->value, 7);
}

TEST(World, AddRemoveComponent_MovesArchetype)
{
    World world;
    const Entity e = world.CreateEntity(Position{1, 2, 3});
    const auto before = world.ArchetypeOf(e);

    ASSERT_TRUE(world.Add(e, Velocity{4, 5, 6}));
    const auto withVelocity = world.ArchetypeOf(e);
    EXPECT_NE(withVelocity, before);
    EXPECT_EQ(world.Get<Position>(e)->x, 1.0f); // survived the move
    EXPECT_EQ(world.Get<Velocity>(e)->z, 6.0f);

    ASSERT_TRUE(world.Remove<Velocity>(e));
    EXPECT_EQ(world.ArchetypeOf(e), before); // cached edge leads back to the same archetype
    EXPECT_FALSE(world.Has<Velocity>(e));
    EXPECT_EQ(world.Get<Position>(e)->z, 3.0f);
}

TEST(World, AddExisting_Assigns)
{
    World world;
    const Entity e = world.CreateEntity(Health{1});
    const auto archetype = world.ArchetypeOf(e);
    ASSERT_TRUE(world.Add(e, Health{2}));
    EXPECT_EQ(world.ArchetypeOf(e), archetype);
    EXPECT_EQ(world.Get<Health>(e)->value, 2);
}

TEST(World, RemoveMissing_ReturnsFalse)
{
    World world;
    const Entity e = world.CreateEntity(Health{1});
    EXPECT_FALSE(world.Remove<Position>(e));
    EXPECT_TRUE(world.Has<Health>(e));
}

TEST(World, Destroy_InvalidatesHandle)
{
    World world;
    const Entity e = world.CreateEntity(Health{1});
    EXPECT_TRUE(world.DestroyEntity(e));
    EXPECT_FALSE(world.IsAlive(e));
    EXPECT_FALSE(world.DestroyEntity(e));
    EXPECT_EQ(world.Get<Health>(e), nullptr);
    EXPECT_FALSE(world.Add(e, Position{}));
    EXPECT_EQ(world.EntityCount(), 0u);

    const Entity reused = world.CreateEntity(Health{2});
    EXPECT_NE(reused, e); // generation differs even if the slot is reused
    EXPECT_FALSE(world.IsAlive(e));
}

TEST(World, Tags_HaveNoStorageButAreTracked)
{
    World world;
    const Entity e = world.CreateEntity(Position{}, StaticTag{});
    EXPECT_TRUE(world.Has<StaticTag>(e));
    ASSERT_TRUE(world.Remove<StaticTag>(e));
    EXPECT_FALSE(world.Has<StaticTag>(e));
    ASSERT_TRUE(world.Add(e, StaticTag{}));
    EXPECT_TRUE(world.Has<StaticTag>(e));
}

TEST(World, ManyEntities_SpanChunksAndSurviveSwapRemove)
{
    World world;
    std::vector<Entity> entities;
    constexpr int kCount = 5000; // several 16 KiB chunks
    for (int i = 0; i < kCount; ++i)
    {
        entities.push_back(world.CreateEntity(Position{static_cast<float>(i), 0, 0}, Health{i}));
    }
    EXPECT_GT(world.ChunkCount(world.ArchetypeOf(entities[0])), 1u);

    // Destroy every third entity; the rest must keep their values though rows are swapped around.
    for (std::size_t i = 0; i < entities.size(); i += 3)
    {
        ASSERT_TRUE(world.DestroyEntity(entities[i]));
    }
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        if (i % 3 == 0)
        {
            EXPECT_FALSE(world.IsAlive(entities[i]));
            continue;
        }
        ASSERT_TRUE(world.IsAlive(entities[i]));
        EXPECT_EQ(world.Get<Health>(entities[i])->value, static_cast<int>(i));
        EXPECT_EQ(world.Get<Position>(entities[i])->x, static_cast<float>(i));
    }
}

TEST(World, NonTrivialComponents_DestroyedExactlyOnce)
{
    Tracked::s_live = 0;
    {
        World world;
        std::vector<Entity> entities;
        for (int i = 0; i < 1000; ++i)
        {
            entities.push_back(world.CreateEntity(Tracked{i}));
        }
        EXPECT_EQ(Tracked::s_live, 1000);
        for (int i = 0; i < 1000; i += 2)
        {
            ASSERT_TRUE(world.Add(entities[static_cast<std::size_t>(i)], Position{}));
        }
        EXPECT_EQ(Tracked::s_live, 1000);
        for (int i = 0; i < 1000; i += 4)
        {
            ASSERT_TRUE(world.Remove<Tracked>(entities[static_cast<std::size_t>(i)]));
        }
        EXPECT_EQ(Tracked::s_live, 750);
        for (int i = 1; i < 1000; i += 4)
        {
            ASSERT_TRUE(world.DestroyEntity(entities[static_cast<std::size_t>(i)]));
        }
        EXPECT_EQ(Tracked::s_live, 500);
        EXPECT_EQ(*world.Get<Tracked>(entities[2])->value, 2);
        EXPECT_EQ(*world.Get<Tracked>(entities[3])->value, 3);
    }
    EXPECT_EQ(Tracked::s_live, 0); // world destructor destroys the rest
}

TEST(World, RowLargerThanChunk_StillStored)
{
    World world;
    auto big = std::make_unique<Big>();
    big->bytes[39999] = std::byte{7};
    const Entity a = world.CreateEntity(*big);
    const Entity b = world.CreateEntity(*big);
    EXPECT_EQ(world.Get<Big>(a)->bytes[39999], std::byte{7});
    EXPECT_EQ(world.Get<Big>(b)->bytes[39999], std::byte{7});
}

TEST(World, ChunkMemory_ComesFromWorldAllocator)
{
    CountingAllocator counting;
    {
        World world(counting);
        (void)world.CreateEntity(Position{});
        EXPECT_GT(counting.liveBytes, 0u);
    }
    EXPECT_EQ(counting.liveBytes, 0u);
}
