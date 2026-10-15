// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction_cook/CookConfig.h>
#include <interi/destruction_cook/Cooker.h>
#include <interi/destruction_cook/GltfImport.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <meshoptimizer.h>

#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace interi::destruction
{
namespace
{

Result<std::string> ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot open '{}'", path.string()));
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

/// True if `path` holds a valid asset (payload hash included) cooked with `cookKey`.
bool IsUpToDate(const std::filesystem::path& path, std::uint64_t cookKey)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto data = std::as_bytes(std::span(bytes));
    const Result<std::uint64_t> key = ReadCookKey(data);
    return key && *key == cookKey && ReadDestructibleAsset(data).has_value();
}

Result<> WriteFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file ||
            !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        {
            return MakeError(ErrorCode::IoError, std::format("cannot write '{}'", temporary.string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot replace '{}': {}", path.string(), error.message()));
    }
    return {};
}

void AppendHierarchy(std::vector<Chunk>& all, ChunkHierarchy hierarchy)
{
    const auto offset = static_cast<std::uint32_t>(all.size());
    for (Chunk& chunk : hierarchy.chunks)
    {
        if (chunk.parent != kNoChunk)
        {
            chunk.parent += offset;
        }
        if (chunk.childCount > 0)
        {
            chunk.firstChild += offset;
        }
        all.push_back(std::move(chunk));
    }
}

} // namespace

Result<CookReport> Cook(const CookRequest& request, JobSystem& jobs)
{
    CookReport report;
    report.output = request.output;
    if (report.output.empty())
    {
        report.output = request.input;
        report.output.replace_extension(".idest");
    }

    std::filesystem::path configPath = request.config;
    if (configPath.empty())
    {
        const auto sidecar = request.input.parent_path() / (request.input.stem().string() + ".fracture.json");
        if (std::filesystem::exists(sidecar))
        {
            configPath = sidecar;
        }
    }
    std::string configText;
    CookConfig config = DefaultCookConfig();
    if (!configPath.empty())
    {
        INTERI_TRY_ASSIGN(text, ReadText(configPath));
        configText = std::move(text);
        INTERI_TRY_ASSIGN(parsed, ParseCookConfig(configText));
        config = std::move(parsed);
    }
    report.warnings = config.warnings;

    std::filesystem::path materialsPath = request.materials;
    if (materialsPath.empty())
    {
        if (config.materialsFile.empty())
        {
            return MakeError(ErrorCode::InvalidArgument,
                             "no materials file: pass one or set \"materialsFile\" in the fracture config");
        }
        materialsPath = configPath.parent_path() / config.materialsFile;
    }
    INTERI_TRY_ASSIGN(materialsText, ReadText(materialsPath));
    INTERI_TRY_ASSIGN(materialSet, ParseMaterials(materialsText));
    report.warnings.insert(report.warnings.end(), materialSet.warnings.begin(), materialSet.warnings.end());

    INTERI_TRY_ASSIGN(scene, ImportGltf(request.input));
    report.cookKey =
        HashCombine(HashCombine(HashCombine(HashCombine(kCookerVersion, kDestructibleVersion), scene.contentHash),
                                Hash64(configText)),
                    Hash64(materialsText));
    if (!request.force && IsUpToDate(report.output, report.cookKey))
    {
        report.skipped = true;
        return report;
    }

    std::vector<Chunk> chunks;
    std::vector<FractureMesh> anchorMeshes;
    for (SourceObject& object : scene.objects)
    {
        INTERI_TRY_ASSIGN(settings, config.SettingsFor(object.name, object.firstMaterialName));
        if (settings.skip)
        {
            continue;
        }
        const std::optional<std::uint32_t> material = FindMaterial(materialSet.materials, settings.material);
        if (!material)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("object '{}': unknown material '{}'", object.name, settings.material));
        }
        for (FractureTriangle& triangle : object.mesh.triangles)
        {
            triangle.material = *material;
        }
        if (settings.anchor)
        {
            anchorMeshes.push_back(std::move(object.mesh));
            continue;
        }
        const float minSize = materialSet.materials[*material].debrisMinSize;
        HierarchySettings hierarchy;
        hierarchy.levels = settings.levels;
        hierarchy.maxDepth = static_cast<std::uint32_t>(settings.levels.size());
        hierarchy.voronoi.minPieceVolume =
            settings.minPieceVolume.value_or(std::pow(static_cast<double>(minSize), 3.0));
        AppendHierarchy(chunks, BuildHierarchy(object.mesh, hierarchy, jobs));
    }
    if (chunks.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "no destructible objects to cook");
    }

    BondSettings bondSettings;
    bondSettings.contact = config.contact;
    bondSettings.groundHeight = config.groundHeight;
    BondGraph bonds = BuildBondGraph(chunks, materialSet.materials, bondSettings, anchorMeshes);
    INTERI_TRY_ASSIGN(asset,
                      AssembleAsset(chunks, std::move(bonds), std::move(materialSet.materials), config.hull, jobs));
    ParallelFor(jobs, 0, asset.chunks.size(), 4, [&](std::size_t c) { OptimizeRenderMesh(asset.chunks[c].render); });

    INTERI_TRY(WriteFileAtomically(report.output, WriteDestructibleAsset(asset, report.cookKey)));
    report.chunkCount = asset.chunks.size();
    report.bondCount = asset.bonds.size();
    report.anchorCount = asset.anchors.size();
    return report;
}

Result<DestructibleAsset> LoadDestructibleAsset(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot open '{}'", path.string()));
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return ReadDestructibleAsset(std::as_bytes(std::span(bytes)));
}

void OptimizeRenderMesh(RenderMesh& mesh)
{
    if (mesh.indices.empty())
    {
        return;
    }
    std::vector<std::uint32_t> reordered(mesh.indices.size());
    for (const Submesh& submesh : mesh.submeshes)
    {
        meshopt_optimizeVertexCache(reordered.data() + submesh.indexOffset, mesh.indices.data() + submesh.indexOffset,
                                    submesh.indexCount, mesh.vertices.size());
    }
    mesh.indices = std::move(reordered);

    std::vector<RenderVertex> vertices(mesh.vertices.size());
    const std::size_t used =
        meshopt_optimizeVertexFetch(vertices.data(), mesh.indices.data(), mesh.indices.size(), mesh.vertices.data(),
                                    mesh.vertices.size(), sizeof(RenderVertex));
    vertices.resize(used);
    mesh.vertices = std::move(vertices);
}

} // namespace interi::destruction
