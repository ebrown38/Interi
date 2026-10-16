// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>

namespace interi::destruction
{

namespace
{

struct Appender
{
    const ChunkMaterials& materials;
    std::vector<render::RenderObject>& out;
    std::size_t maxObjects;
    ChunkGatherStats& stats;

    void Chunks(const AssetMeshes& meshes, std::span<const std::uint32_t> chunks, const Mat4& transform, float opacity,
                bool occluder)
    {
        for (const std::uint32_t chunk : chunks)
        {
            ++stats.chunks;
            for (const ChunkSubmesh& submesh : meshes.Submeshes(chunk))
            {
                if (out.size() >= maxObjects)
                {
                    ++stats.dropped;
                    continue;
                }
                render::Material material = materials.Get(submesh);
                material.opacity *= opacity;
                out.push_back({.mesh = submesh.mesh,
                               .transform = transform,
                               .material = material,
                               .castsShadow = true,
                               .occluder = occluder});
            }
        }
    }
};

} // namespace

ChunkGatherer::ChunkGatherer()
{
    m_debris.With<Transform, DebrisSourceComponent, DebrisComponent>();
    m_rubble.With<Transform, RubbleSourceComponent>();
}

ChunkGatherStats ChunkGatherer::Gather(World& world, const DestructionWorld& destruction, ChunkMeshCache& cache,
                                       const ChunkMaterials& materials, std::vector<render::RenderObject>& out,
                                       std::size_t maxObjects)
{
    ChunkGatherStats stats;
    Appender append{materials, out, maxObjects, stats};

    for (const DestructibleId id : destruction.Instances())
    {
        AssetMeshes* meshes = cache.Find(&destruction.Asset(id));
        const std::uint32_t pieceCount = destruction.PieceCount(id);
        if (meshes == nullptr)
        {
            stats.uncached += pieceCount;
            continue;
        }
        for (std::uint32_t piece = 0; piece < pieceCount; ++piece)
        {
            const std::span<const std::uint32_t> chunks = destruction.PieceChunks(id, piece);
            if (chunks.empty())
            {
                continue;
            }
            const Entity entity = destruction.PieceEntity(id, piece);
            const Transform* transform = entity.IsValid() ? world.Get<Transform>(entity) : nullptr;
            const bool anchored = destruction.PieceAnchored(id, piece);
            if (transform == nullptr && !anchored)
            {
                continue; // a piece without a body (no collision hulls) has no pose
            }
            const Transform pose = transform != nullptr ? *transform : destruction.InstanceTransform(id);
            m_cover.clear();
            meshes->renderSet.Cover(chunks, m_cover);
            append.Chunks(*meshes, m_cover, pose.ToMat4(), 1.0f, anchored);
            ++stats.pieces;
        }
    }

    m_debris.ForEachChunk(world,
                          [&](ChunkView& view)
                          {
                              const auto transforms = view.Read<Transform>();
                              const auto sources = view.Read<DebrisSourceComponent>();
                              const auto debris = view.Read<DebrisComponent>();
                              for (std::uint32_t i = 0; i < view.Count(); ++i)
                              {
                                  if (!debris[i].visible)
                                  {
                                      continue;
                                  }
                                  AssetMeshes* meshes = cache.Find(sources[i].asset.get());
                                  if (meshes == nullptr)
                                  {
                                      ++stats.uncached;
                                      continue;
                                  }
                                  m_cover.clear();
                                  meshes->renderSet.Cover(sources[i].chunks, m_cover);
                                  append.Chunks(*meshes, m_cover, transforms[i].ToMat4(), debris[i].opacity, false);
                                  ++stats.debris;
                              }
                          });

    m_rubble.ForEachChunk(world,
                          [&](ChunkView& view)
                          {
                              const auto transforms = view.Read<Transform>();
                              const auto sources = view.Read<RubbleSourceComponent>();
                              for (std::uint32_t i = 0; i < view.Count(); ++i)
                              {
                                  for (const RubblePart& part : sources[i].parts)
                                  {
                                      AssetMeshes* meshes = cache.Find(part.asset.get());
                                      if (meshes == nullptr)
                                      {
                                          ++stats.uncached;
                                          continue;
                                      }
                                      Transform local;
                                      local.position = part.position;
                                      local.rotation = part.rotation;
                                      m_cover.clear();
                                      meshes->renderSet.Cover(part.chunks, m_cover);
                                      append.Chunks(*meshes, m_cover, transforms[i].ToMat4() * local.ToMat4(), 1.0f,
                                                    false);
                                      ++stats.rubble;
                                  }
                              }
                          });
    return stats;
}

} // namespace interi::destruction
