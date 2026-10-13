// SPDX-License-Identifier: MIT
// Compiles every public ecs header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and instantiates the templates.
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/ecs/World.h>

namespace interi
{

namespace
{

struct HeaderCheckComponent
{
    int value;
};

struct HeaderCheckTag
{
};

// Instantiates the World templates once so clang-tidy checks their bodies. Never called.
[[maybe_unused]] void InstantiateWorld(World& world)
{
    const Entity entity = world.CreateEntity(HeaderCheckComponent{1}, HeaderCheckTag{});
    (void)world.Add(entity, HeaderCheckComponent{2});
    (void)world.Has<HeaderCheckTag>(entity);
    (void)world.Get<HeaderCheckComponent>(entity);
    (void)world.GetMut<HeaderCheckComponent>(entity);
    (void)world.Remove<HeaderCheckTag>(entity);
}

// Instantiates the Query and CommandBuffer templates. Never called.
[[maybe_unused]] void InstantiateQueries(World& world, JobSystem& jobs, CommandBuffers& commands, Entity entity)
{
    Query query;
    query.With<HeaderCheckComponent>().Without<HeaderCheckTag>().Changed<HeaderCheckComponent>();
    SystemAccess access;
    access.Read<HeaderCheckTag>().Write<HeaderCheckComponent>();
    query.ForEachChunk(world, [](ChunkView& view) { (void)view.Write<HeaderCheckComponent>(); });
    query.ParallelForEachChunk(
        world, jobs, RunTicks{}, [](ChunkView& view, std::size_t) { (void)view.Read<HeaderCheckComponent>(); },
        &access);
    CommandBuffer& buffer = commands.Local(jobs);
    buffer.CreateEntity(0, HeaderCheckComponent{1}, HeaderCheckTag{});
    buffer.Add(1, entity, HeaderCheckComponent{2});
    buffer.Remove<HeaderCheckTag>(2, entity);
}

// Instantiates the SystemContext templates. Never called.
[[maybe_unused]] void InstantiateSystemContext(const SystemContext& context, const Query& query)
{
    context.ForEachChunk(query, [](ChunkView& view) { (void)view.Count(); });
    context.ParallelForEachChunk(query, [](ChunkView& view, std::size_t) { (void)view.Count(); });
}

} // namespace

} // namespace interi
