// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/core/Hash.h>
#include <interi/nav/NavMesh.h>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace interi::nav
{

namespace
{

constexpr FourCC kMagic = MakeFourCC("INAV");
constexpr FourCC kPayloadMagic = MakeFourCC("NAVP");
constexpr std::uint32_t kVersion = 1;
constexpr std::uint8_t kWalkFlag = 1;
constexpr int kMaxPathPolys = 256;
constexpr int kMaxStraightPoints = 256;

constexpr int kMaxTileItems = 1 << 20;

static_assert(DT_NAVMESH_VERSION == 7,
              "ValidateTileData mirrors Detour 1.6.0's tile layout: review it when Detour changes");

std::optional<std::size_t> Aligned4Size(int count, std::size_t elementSize)
{
    if (count < 0 || count > kMaxTileItems)
    {
        return std::nullopt;
    }
    return (static_cast<std::size_t>(count) * elementSize + 3U) & ~std::size_t{3};
}

template <class T>
T ReadAt(const std::byte* base, std::size_t offset, std::size_t index = 0)
{
    T value;
    const std::size_t byteOffset = offset + index * sizeof(T); // base is a byte pointer: this is a byte count
    std::memcpy(&value, base + byteOffset, sizeof(T));
    return value;
}

bool FiniteFloats(const std::byte* base, std::size_t offset, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!std::isfinite(ReadAt<float>(base, offset, i)))
        {
            return false;
        }
    }
    return true;
}

// Detour trusts a tile's header and indexes its arrays without checks, so every count, section size and index a
// query follows is verified here before the tile is handed over.
Result<void> ValidateTileData(std::span<const std::byte> data)
{
    const auto corrupt = [](const char* what)
    { return MakeError(ErrorCode::Corrupt, std::string("navigation tile: ") + what); };
    if (data.size() < sizeof(dtMeshHeader) || data.size() > static_cast<std::size_t>(INT_MAX) || data.size() % 4 != 0)
    {
        return corrupt("size");
    }
    dtMeshHeader h;
    std::memcpy(&h, data.data(), sizeof h);
    if (h.magic != DT_NAVMESH_MAGIC || h.version != DT_NAVMESH_VERSION)
    {
        return corrupt("header");
    }
    if (h.x != 0 || h.y != 0 || h.layer != 0)
    {
        return corrupt("tile position");
    }
    if (h.maxLinkCount < 1 || h.polyCount < 1 || h.vertCount < 1 || h.detailMeshCount != h.offMeshBase ||
        h.offMeshBase < 0 || h.offMeshBase > h.polyCount || h.offMeshConCount > h.polyCount - h.offMeshBase)
    {
        return corrupt("counts");
    }
    const auto vertsSize = Aligned4Size(h.vertCount, sizeof(float) * 3);
    const auto polysSize = Aligned4Size(h.polyCount, sizeof(dtPoly));
    const auto linksSize = Aligned4Size(h.maxLinkCount, sizeof(dtLink));
    const auto detailMeshesSize = Aligned4Size(h.detailMeshCount, sizeof(dtPolyDetail));
    const auto detailVertsSize = Aligned4Size(h.detailVertCount, sizeof(float) * 3);
    const auto detailTrisSize = Aligned4Size(h.detailTriCount, 4);
    const auto bvSize = Aligned4Size(h.bvNodeCount, sizeof(dtBVNode));
    const auto offMeshSize = Aligned4Size(h.offMeshConCount, sizeof(dtOffMeshConnection));
    if (!vertsSize || !polysSize || !linksSize || !detailMeshesSize || !detailVertsSize || !detailTrisSize || !bvSize ||
        !offMeshSize)
    {
        return corrupt("counts");
    }
    const std::size_t headerSize = (sizeof(dtMeshHeader) + 3U) & ~std::size_t{3};
    const std::size_t vertsAt = headerSize;
    const std::size_t polysAt = vertsAt + *vertsSize;
    const std::size_t linksAt = polysAt + *polysSize;
    const std::size_t detailMeshesAt = linksAt + *linksSize;
    const std::size_t detailVertsAt = detailMeshesAt + *detailMeshesSize;
    const std::size_t detailTrisAt = detailVertsAt + *detailVertsSize;
    const std::size_t bvAt = detailTrisAt + *detailTrisSize;
    const std::size_t offMeshAt = bvAt + *bvSize;
    if (offMeshAt + *offMeshSize != data.size())
    {
        return corrupt("section sizes do not match the data size");
    }
    const std::byte* base = data.data();
    if (!FiniteFloats(base, offsetof(dtMeshHeader, bmin), 6) || !std::isfinite(h.bvQuantFactor) ||
        !FiniteFloats(base, vertsAt, static_cast<std::size_t>(h.vertCount) * 3) ||
        !FiniteFloats(base, detailVertsAt, static_cast<std::size_t>(h.detailVertCount) * 3))
    {
        return corrupt("non-finite coordinates");
    }
    if (!(h.bmax[0] > h.bmin[0]) || !(h.bmax[2] > h.bmin[2]))
    {
        return corrupt("tile bounds");
    }
    // Queries quantize a box clamped to the tile bounds as (unsigned short)(bvQuantFactor * extent + 1).
    const auto quantizable = [&](int axis)
    {
        const float extent = h.bmax[axis] - h.bmin[axis];
        return std::isfinite(extent) && extent >= 0.0f && h.bvQuantFactor * extent <= 65534.0f;
    };
    if (h.bvNodeCount != 0 && (!(h.bvQuantFactor > 0.0f) || !quantizable(0) || !quantizable(1) || !quantizable(2)))
    {
        return corrupt("bounding volume quantization");
    }
    const auto polyCount = static_cast<unsigned>(h.polyCount);
    for (std::size_t i = 0; i < polyCount; ++i)
    {
        const auto poly = ReadAt<dtPoly>(base, polysAt, i);
        // Detour reads verts[0] and verts[1] of an off-mesh polygon unchecked, so the type must match the range.
        const bool offMesh = poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION;
        if (offMesh != (std::cmp_greater_equal(i, h.offMeshBase)))
        {
            return corrupt("polygon type");
        }
        if (poly.vertCount > DT_VERTS_PER_POLYGON || (offMesh ? poly.vertCount != 2 : poly.vertCount < 3))
        {
            return corrupt("polygon vertex count");
        }
        for (unsigned k = 0; k < poly.vertCount; ++k)
        {
            if (std::cmp_greater_equal(poly.verts[k], h.vertCount))
            {
                return corrupt("polygon vertex index");
            }
            const unsigned nei = poly.neis[k];
            if ((nei & DT_EXT_LINK) == 0 && nei > polyCount)
            {
                return corrupt("polygon neighbour");
            }
        }
        if (offMesh)
        {
            continue; // Detour gives only ground polygons (the first offMeshBase) a detail mesh
        }
        const auto detail = ReadAt<dtPolyDetail>(base, detailMeshesAt, i);
        if (detail.triCount == 0 ||
            std::uint64_t{detail.vertBase} + detail.vertCount > static_cast<unsigned>(h.detailVertCount) ||
            std::uint64_t{detail.triBase} + detail.triCount > static_cast<unsigned>(h.detailTriCount))
        {
            return corrupt("detail mesh range");
        }
        // closestPointOnPoly interpolates along the nearest boundary edge of the detail mesh and dereferences null
        // when there is none.
        constexpr unsigned kAnyBoundaryEdge =
            (DT_DETAIL_EDGE_BOUNDARY << 0) | (DT_DETAIL_EDGE_BOUNDARY << 2) | (DT_DETAIL_EDGE_BOUNDARY << 4);
        bool boundaryEdge = false;
        for (unsigned t = 0; t < detail.triCount; ++t)
        {
            const std::size_t tri = detailTrisAt + (std::size_t{detail.triBase} + t) * 4;
            boundaryEdge = boundaryEdge || (std::to_integer<unsigned>(base[tri + 3]) & kAnyBoundaryEdge) != 0;
            for (unsigned k = 0; k < 3; ++k)
            {
                if (std::to_integer<unsigned>(base[tri + k]) >= unsigned{poly.vertCount} + detail.vertCount)
                {
                    return corrupt("detail triangle index");
                }
            }
        }
        if (!boundaryEdge)
        {
            return corrupt("detail mesh without a boundary edge");
        }
    }
    for (int i = 0; i < h.bvNodeCount; ++i)
    {
        const auto node = ReadAt<dtBVNode>(base, bvAt, static_cast<std::size_t>(i));
        const bool leaf = node.i >= 0;
        if (leaf ? node.i >= h.polyCount : (node.i == INT_MIN || -node.i > h.bvNodeCount - i))
        {
            return corrupt("bounding volume node");
        }
    }
    for (int i = 0; i < h.offMeshConCount; ++i)
    {
        const unsigned conPoly = ReadAt<dtOffMeshConnection>(base, offMeshAt, static_cast<std::size_t>(i)).poly;
        if (std::cmp_less(conPoly, h.offMeshBase) || conPoly >= polyCount ||
            !FiniteFloats(base, offMeshAt + static_cast<std::size_t>(i) * sizeof(dtOffMeshConnection), 7))
        {
            return corrupt("off-mesh connection");
        }
    }
    return {};
}

/// Recast logs through a context; the engine reports failures through Result instead.
class SilentContext final : public rcContext
{
public:
    SilentContext()
        : rcContext(false)
    {
    }
};

struct HeightfieldDeleter
{
    void operator()(rcHeightfield* p) const noexcept { rcFreeHeightField(p); }
};
struct CompactDeleter
{
    void operator()(rcCompactHeightfield* p) const noexcept { rcFreeCompactHeightfield(p); }
};
struct ContourDeleter
{
    void operator()(rcContourSet* p) const noexcept { rcFreeContourSet(p); }
};
struct PolyMeshDeleter
{
    void operator()(rcPolyMesh* p) const noexcept { rcFreePolyMesh(p); }
};
struct DetailDeleter
{
    void operator()(rcPolyMeshDetail* p) const noexcept { rcFreePolyMeshDetail(p); }
};
struct QueryDeleter
{
    void operator()(dtNavMeshQuery* p) const noexcept { dtFreeNavMeshQuery(p); }
};

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool ValidSettings(const NavBakeSettings& s) noexcept
{
    const auto positive = [](float v) { return std::isfinite(v) && v > 0.0f; };
    return positive(s.cellSize) && positive(s.cellHeight) && positive(s.agentHeight) && positive(s.agentMaxClimb) &&
           std::isfinite(s.agentRadius) && s.agentRadius >= 0.0f && std::isfinite(s.agentMaxSlopeDegrees) &&
           s.agentMaxSlopeDegrees > 0.0f && s.agentMaxSlopeDegrees < 90.0f && positive(s.maxEdgeLength) &&
           std::isfinite(s.maxSimplificationError) && s.maxSimplificationError >= 0.0f && s.minRegionArea >= 0 &&
           s.mergeRegionArea >= 0 && std::isfinite(s.detailSampleDistance) && s.detailSampleDistance >= 0.0f &&
           std::isfinite(s.detailSampleMaxError) && s.detailSampleMaxError >= 0.0f;
}

std::array<float, 3> ToArray(const Vec3& v) noexcept
{
    return {v.x, v.y, v.z};
}

} // namespace

template <typename Archive>
void Serialize(Archive& archive, NavBakeSettings& s)
{
    archive(s.cellSize, s.cellHeight, s.agentHeight, s.agentRadius, s.agentMaxClimb, s.agentMaxSlopeDegrees,
            s.maxEdgeLength, s.maxSimplificationError, s.minRegionArea, s.mergeRegionArea, s.detailSampleDistance,
            s.detailSampleMaxError);
}

float NavPath::Length() const noexcept
{
    float total = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        total += interi::Length(points[i] - points[i - 1]);
    }
    return total;
}

void NavMesh::Deleter::operator()(dtNavMesh* mesh) const noexcept
{
    dtFreeNavMesh(mesh);
}

NavMesh::NavMesh(std::unique_ptr<dtNavMesh, Deleter> mesh, std::vector<std::byte> data, const NavBakeSettings& settings)
    : m_mesh(std::move(mesh))
    , m_data(std::move(data))
    , m_settings(settings)
{
}

NavMesh::NavMesh(NavMesh&&) noexcept = default;
NavMesh& NavMesh::operator=(NavMesh&&) noexcept = default;
NavMesh::~NavMesh() = default;

Result<NavMesh> NavMesh::FromData(std::vector<std::byte> data, const NavBakeSettings& settings)
{
    INTERI_TRY(ValidateTileData(data));
    // Detour keeps the tile and frees it (DT_TILE_FREE_DATA), so it gets its own copy from dtAlloc.
    auto* tile = static_cast<unsigned char*>(dtAlloc(data.size(), DT_ALLOC_PERM));
    if (tile == nullptr)
    {
        return MakeError(ErrorCode::OutOfMemory, "navigation tile");
    }
    std::memcpy(tile, data.data(), data.size());
    std::unique_ptr<dtNavMesh, Deleter> mesh(dtAllocNavMesh());
    if (!mesh || dtStatusFailed(mesh->init(tile, static_cast<int>(data.size()), DT_TILE_FREE_DATA)))
    {
        dtFree(tile);
        return MakeError(ErrorCode::Corrupt, "navigation data rejected by Detour");
    }
    return NavMesh(std::move(mesh), std::move(data), settings);
}

Result<NavMesh> NavMesh::Bake(std::span<const Vec3> vertices, std::span<const std::uint32_t> indices,
                              const NavBakeSettings& settings)
{
    if (!ValidSettings(settings))
    {
        return Invalid("navigation bake: invalid settings");
    }
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0)
    {
        return Invalid("navigation bake: needs triangles");
    }
    for (const std::uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            return Invalid("navigation bake: triangle index out of range");
        }
    }
    for (const Vec3& v : vertices)
    {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        {
            return Invalid("navigation bake: non-finite vertex");
        }
    }

    const int vertexCount = static_cast<int>(vertices.size());
    const int triangleCount = static_cast<int>(indices.size() / 3);
    std::vector<float> positions;
    positions.reserve(vertices.size() * 3);
    for (const Vec3& v : vertices)
    {
        positions.insert(positions.end(), {v.x, v.y, v.z});
    }
    std::vector<int> triangles(indices.begin(), indices.end());

    rcConfig config{};
    config.cs = settings.cellSize;
    config.ch = settings.cellHeight;
    config.walkableSlopeAngle = settings.agentMaxSlopeDegrees;
    config.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / settings.cellHeight));
    config.walkableClimb = static_cast<int>(std::floor(settings.agentMaxClimb / settings.cellHeight));
    config.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / settings.cellSize));
    config.maxEdgeLen = static_cast<int>(settings.maxEdgeLength / settings.cellSize);
    config.maxSimplificationError = settings.maxSimplificationError;
    config.minRegionArea = settings.minRegionArea * settings.minRegionArea;
    config.mergeRegionArea = settings.mergeRegionArea * settings.mergeRegionArea;
    config.maxVertsPerPoly = 6;
    config.detailSampleDist =
        settings.detailSampleDistance < 0.9f ? 0.0f : settings.cellSize * settings.detailSampleDistance;
    config.detailSampleMaxError = settings.cellHeight * settings.detailSampleMaxError;
    rcCalcBounds(positions.data(), vertexCount, config.bmin, config.bmax);
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    if (config.width <= 0 || config.height <= 0)
    {
        return MakeError(ErrorCode::Degenerate, "navigation bake: the geometry has no area");
    }
    if (config.width > 4096 || config.height > 4096)
    {
        return Invalid(std::format("navigation bake: a {} x {} cell grid is out of range (raise cellSize)",
                                   config.width, config.height));
    }

    SilentContext context;
    std::unique_ptr<rcHeightfield, HeightfieldDeleter> heightfield(rcAllocHeightfield());
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin,
                                             config.bmax, config.cs, config.ch))
    {
        return MakeError(ErrorCode::OutOfMemory, "navigation bake: heightfield");
    }
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount), 0);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, positions.data(), vertexCount, triangles.data(),
                            triangleCount, areas.data());
    if (!rcRasterizeTriangles(&context, positions.data(), vertexCount, triangles.data(), areas.data(), triangleCount,
                              *heightfield, config.walkableClimb))
    {
        return MakeError(ErrorCode::OutOfMemory, "navigation bake: rasterize");
    }
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    std::unique_ptr<rcCompactHeightfield, CompactDeleter> compact(rcAllocCompactHeightfield());
    if (!compact ||
        !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact) ||
        !rcErodeWalkableArea(&context, config.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea))
    {
        return MakeError(ErrorCode::Degenerate, "navigation bake: nothing walkable");
    }
    std::unique_ptr<rcContourSet, ContourDeleter> contours(rcAllocContourSet());
    if (!contours ||
        !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) ||
        contours->nconts == 0)
    {
        return MakeError(ErrorCode::Degenerate, "navigation bake: nothing walkable");
    }
    std::unique_ptr<rcPolyMesh, PolyMeshDeleter> polys(rcAllocPolyMesh());
    std::unique_ptr<rcPolyMeshDetail, DetailDeleter> detail(rcAllocPolyMeshDetail());
    if (!polys || !detail || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polys) ||
        !rcBuildPolyMeshDetail(&context, *polys, *compact, config.detailSampleDist, config.detailSampleMaxError,
                               *detail) ||
        polys->npolys == 0)
    {
        return MakeError(ErrorCode::Degenerate, "navigation bake: nothing walkable");
    }
    for (int i = 0; i < polys->npolys; ++i)
    {
        polys->areas[i] = RC_WALKABLE_AREA;
        polys->flags[i] = kWalkFlag;
    }

    dtNavMeshCreateParams params{};
    params.verts = polys->verts;
    params.vertCount = polys->nverts;
    params.polys = polys->polys;
    params.polyAreas = polys->areas;
    params.polyFlags = polys->flags;
    params.polyCount = polys->npolys;
    params.nvp = polys->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb = settings.agentMaxClimb;
    rcVcopy(params.bmin, polys->bmin);
    rcVcopy(params.bmax, polys->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &data, &size))
    {
        return MakeError(ErrorCode::Degenerate, "navigation bake: Detour rejected the polygons");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::memcpy(bytes.data(), data, bytes.size());
    dtFree(data);
    return FromData(std::move(bytes), settings);
}

std::vector<std::byte> NavMesh::Write() const
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kPayloadMagic, kVersion);
        NavBakeSettings settings = m_settings;
        std::vector<std::byte> data = m_data;
        writer(settings, data);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kMagic, kVersion);
    writer(payloadHash, payload);
    return file;
}

Result<NavMesh> NavMesh::Read(std::span<const std::byte> bytes)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(bytes, kMagic, kVersion));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "navigation mesh payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kPayloadMagic, kVersion));
    NavBakeSettings settings;
    std::vector<std::byte> data;
    reader(settings, data);
    INTERI_TRY(reader.Finish());
    if (data.empty() || !ValidSettings(settings))
    {
        return MakeError(ErrorCode::Corrupt, "navigation mesh header");
    }
    return FromData(std::move(data), settings);
}

std::size_t NavMesh::PolygonCount() const noexcept
{
    const dtNavMesh* mesh = m_mesh.get();
    const dtMeshTile* tile = mesh->getTileAt(0, 0, 0);
    return tile != nullptr ? static_cast<std::size_t>(tile->header->polyCount) : 0;
}

Result<Vec3> NavMesh::NearestPoint(const Vec3& point, const Vec3& extents) const
{
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(m_mesh.get(), 16)))
    {
        return MakeError(ErrorCode::OutOfMemory, "navigation query");
    }
    dtQueryFilter filter;
    const auto center = ToArray(point);
    const auto half = ToArray(extents);
    dtPolyRef ref = 0;
    float nearest[3] = {};
    if (dtStatusFailed(query->findNearestPoly(center.data(), half.data(), &filter, &ref, nearest)) || ref == 0)
    {
        return MakeError(ErrorCode::NotFound, "no navigation mesh near the point");
    }
    return Vec3(nearest[0], nearest[1], nearest[2]);
}

Result<NavPath> NavMesh::FindPath(const Vec3& from, const Vec3& to, const Vec3& extents) const
{
    std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(m_mesh.get(), 2048)))
    {
        return MakeError(ErrorCode::OutOfMemory, "navigation query");
    }
    dtQueryFilter filter;
    const auto half = ToArray(extents);
    const auto start = ToArray(from);
    const auto goal = ToArray(to);
    dtPolyRef startRef = 0;
    dtPolyRef goalRef = 0;
    float startPoint[3] = {};
    float goalPoint[3] = {};
    if (dtStatusFailed(query->findNearestPoly(start.data(), half.data(), &filter, &startRef, startPoint)) ||
        startRef == 0 ||
        dtStatusFailed(query->findNearestPoly(goal.data(), half.data(), &filter, &goalRef, goalPoint)) || goalRef == 0)
    {
        return MakeError(ErrorCode::NotFound, "start or goal is off the navigation mesh");
    }
    dtPolyRef corridor[kMaxPathPolys];
    int corridorLength = 0;
    if (dtStatusFailed(query->findPath(startRef, goalRef, startPoint, goalPoint, &filter, corridor, &corridorLength,
                                       kMaxPathPolys)) ||
        corridorLength == 0)
    {
        return MakeError(ErrorCode::NotFound, "no route");
    }
    NavPath path;
    float end[3] = {goalPoint[0], goalPoint[1], goalPoint[2]};
    if (corridor[corridorLength - 1] != goalRef)
    {
        // The goal is in another island: aim at the closest point of the last polygon reached.
        path.partial = true;
        bool onPoly = false;
        if (dtStatusFailed(query->closestPointOnPoly(corridor[corridorLength - 1], goalPoint, end, &onPoly)))
        {
            end[0] = startPoint[0];
            end[1] = startPoint[1];
            end[2] = startPoint[2];
        }
    }
    float straight[kMaxStraightPoints * 3];
    int straightCount = 0;
    if (dtStatusFailed(query->findStraightPath(startPoint, end, corridor, corridorLength, straight, nullptr, nullptr,
                                               &straightCount, kMaxStraightPoints)) ||
        straightCount == 0)
    {
        return MakeError(ErrorCode::NotFound, "no route");
    }
    path.points.reserve(static_cast<std::size_t>(straightCount));
    for (int i = 0; i < straightCount; ++i)
    {
        const auto at = static_cast<std::size_t>(i) * 3;
        path.points.emplace_back(straight[at], straight[at + 1], straight[at + 2]);
    }
    return path;
}

} // namespace interi::nav
