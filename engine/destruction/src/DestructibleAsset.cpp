// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <map>
#include <numeric>

namespace interi::destruction
{
namespace
{

using VertexKey = std::array<std::uint32_t, 8>;

VertexKey KeyOf(const RenderVertex& v) noexcept
{
    return {std::bit_cast<std::uint32_t>(v.position.x), std::bit_cast<std::uint32_t>(v.position.y),
            std::bit_cast<std::uint32_t>(v.position.z), std::bit_cast<std::uint32_t>(v.normal.x),
            std::bit_cast<std::uint32_t>(v.normal.y),   std::bit_cast<std::uint32_t>(v.normal.z),
            std::bit_cast<std::uint32_t>(v.uv.u),       std::bit_cast<std::uint32_t>(v.uv.v)};
}

} // namespace

RenderMesh BuildRenderMesh(const FractureMesh& mesh, std::span<const DestructionMaterial> materials)
{
    const auto surfaceOf = [&](const FractureTriangle& t)
    {
        if (!t.interior)
        {
            return t.surfaceMaterial;
        }
        return t.material < materials.size() ? materials[t.material].interiorMaterialId : 0u;
    };
    std::vector<std::uint32_t> order(mesh.triangles.size());
    std::ranges::iota(order, 0u);
    std::ranges::stable_sort(order, {}, [&](std::uint32_t t) { return surfaceOf(mesh.triangles[t]); });

    RenderMesh render;
    std::map<VertexKey, std::uint32_t> vertexIndex;
    for (const std::uint32_t t : order)
    {
        const FractureTriangle& triangle = mesh.triangles[t];
        const std::uint32_t surface = surfaceOf(triangle);
        if (render.submeshes.empty() || render.submeshes.back().surfaceMaterial != surface)
        {
            render.submeshes.push_back({.surfaceMaterial = surface,
                                        .indexOffset = static_cast<std::uint32_t>(render.indices.size()),
                                        .indexCount = 0});
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            const RenderVertex vertex{
                .position = mesh.positions[triangle.indices[k]], .normal = triangle.normals[k], .uv = triangle.uvs[k]};
            const auto [it, inserted] =
                vertexIndex.try_emplace(KeyOf(vertex), static_cast<std::uint32_t>(render.vertices.size()));
            if (inserted)
            {
                render.vertices.push_back(vertex);
            }
            render.indices.push_back(it->second);
        }
        render.submeshes.back().indexCount += 3;
    }
    return render;
}

std::vector<std::byte> WriteDestructibleAsset(const DestructibleAsset& asset, std::uint64_t cookKey)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kDestructiblePayloadMagic, kDestructibleVersion);
        DestructibleAsset copy = asset; // Serialize takes non-const references (one function for both directions)
        writer(copy);
    }
    std::uint64_t key = cookKey;
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());

    std::vector<std::byte> file;
    ArchiveWriter writer(file, kDestructibleMagic, kDestructibleVersion);
    writer(key, payloadHash, payload);
    return file;
}

Result<DestructibleAsset> AssembleAsset(std::span<const Chunk> chunks, BondGraph bonds,
                                        std::vector<DestructionMaterial> materials, const HullSettings& hullSettings,
                                        JobSystem& jobs)
{
    DestructibleAsset asset;
    asset.chunks.resize(chunks.size());
    std::vector<Error> errors(chunks.size());
    ParallelFor(jobs, 0, chunks.size(), 1,
                [&](std::size_t c)
                {
                    const Chunk& chunk = chunks[c];
                    AssetChunk& out = asset.chunks[c];
                    out.parent = chunk.parent;
                    out.firstChild = chunk.firstChild;
                    out.childCount = chunk.childCount;
                    out.level = chunk.level;
                    out.material = chunk.material;
                    out.volume = static_cast<float>(chunk.volume);
                    out.mass =
                        chunk.material < materials.size() ? out.volume * materials[chunk.material].density : 0.0f;
                    out.centroid = chunk.centroid;
                    out.render = BuildRenderMesh(chunk.mesh, materials);
                    Result<std::vector<CollisionHull>> hulls = BuildCollisionHulls(chunk.mesh, hullSettings);
                    if (hulls)
                    {
                        out.hulls = std::move(*hulls);
                    }
                    else
                    {
                        errors[c] = std::move(hulls.error());
                    }
                });
    for (std::size_t c = 0; c < chunks.size(); ++c)
    {
        if (chunks[c].material >= materials.size())
        {
            return MakeError(ErrorCode::InvalidArgument, std::format("chunk {} has no material", c));
        }
        if (asset.chunks[c].hulls.empty())
        {
            return MakeError(errors[c].code, std::format("chunk {}: {}", c, errors[c].message));
        }
    }
    asset.materials = std::move(materials);
    asset.bonds = std::move(bonds.bonds);
    asset.anchors = std::move(bonds.anchors);
    return asset;
}

Result<DestructibleAsset> ReadDestructibleAsset(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kDestructibleMagic, kDestructibleVersion));
    std::uint64_t cookKey = 0;
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(cookKey, payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "destructible asset payload hash mismatch");
    }

    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kDestructiblePayloadMagic, kDestructibleVersion));
    DestructibleAsset asset;
    reader(asset);
    INTERI_TRY(reader.Finish());
    return asset;
}

Result<std::uint64_t> ReadCookKey(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kDestructibleMagic, kDestructibleVersion));
    std::uint64_t cookKey = 0;
    header(cookKey);
    if (!header.Ok())
    {
        return MakeError(ErrorCode::Corrupt, "destructible asset header is truncated");
    }
    return cookKey;
}

} // namespace interi::destruction
