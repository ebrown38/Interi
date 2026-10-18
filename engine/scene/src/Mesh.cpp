// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/scene/Mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <utility>

namespace interi::scene
{

namespace
{

constexpr FourCC kPayloadMagic = MakeFourCC("IMSP");

[[nodiscard]] bool Finite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

template <typename Archive>
void Serialize(Archive& archive, TriangleMesh& mesh)
{
    archive(mesh.positions, mesh.indices, mesh.normals, mesh.slots);
}

std::uint32_t TriangleMesh::SlotCount() const noexcept
{
    return slots.empty() ? 0 : *std::ranges::max_element(slots) + 1;
}

Result<void> ValidateMesh(const TriangleMesh& mesh)
{
    const auto corrupt = [](std::string message) { return MakeError(ErrorCode::Corrupt, std::move(message)); };
    if (mesh.indices.size() % 3 != 0)
    {
        return corrupt("mesh: index count is not a multiple of 3");
    }
    if (mesh.normals.size() != mesh.indices.size())
    {
        return corrupt("mesh: one normal per index expected");
    }
    if (mesh.slots.size() != mesh.TriangleCount())
    {
        return corrupt("mesh: one slot per triangle expected");
    }
    if (std::ranges::any_of(mesh.indices, [&](std::uint32_t i) { return i >= mesh.positions.size(); }))
    {
        return corrupt("mesh: index out of range");
    }
    if (!std::ranges::all_of(mesh.positions, Finite) || !std::ranges::all_of(mesh.normals, Finite))
    {
        return corrupt("mesh: non-finite position or normal");
    }
    return {};
}

bool IsClosedManifold(const TriangleMesh& mesh)
{
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const std::uint32_t v[3] = {mesh.indices[3 * t], mesh.indices[3 * t + 1], mesh.indices[3 * t + 2]};
        if (v[0] == v[1] || v[1] == v[2] || v[0] == v[2])
        {
            return false;
        }
        for (int e = 0; e < 3; ++e)
        {
            if (++edges[{v[e], v[(e + 1) % 3]}] > 1)
            {
                return false;
            }
        }
    }
    return std::ranges::all_of(edges,
                               [&](const auto& entry)
                               {
                                   const auto reverse = edges.find({entry.first.second, entry.first.first});
                                   return reverse != edges.end() && reverse->second == 1;
                               });
}

double MeshVolume(const TriangleMesh& mesh)
{
    double volume = 0.0;
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const Vec3& a = mesh.positions[mesh.indices[3 * t]];
        const Vec3& b = mesh.positions[mesh.indices[3 * t + 1]];
        const Vec3& c = mesh.positions[mesh.indices[3 * t + 2]];
        const auto wide = [](const Vec3& v) { return std::array<double, 3>{double{v.x}, double{v.y}, double{v.z}}; };
        const auto [ax, ay, az] = wide(a);
        const auto [bx, by, bz] = wide(b);
        const auto [cx, cy, cz] = wide(c);
        volume += ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx);
    }
    return volume / 6.0;
}

MeshBounds BoundsOf(const TriangleMesh& mesh)
{
    MeshBounds bounds;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i)
    {
        const Vec3& p = mesh.positions[i];
        bounds.min = i == 0 ? p : Min(bounds.min, p);
        bounds.max = i == 0 ? p : Max(bounds.max, p);
    }
    return bounds;
}

std::vector<std::byte> WriteMeshAsset(const TriangleMesh& mesh)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kPayloadMagic, kMeshVersion);
        TriangleMesh copy = mesh; // Serialize takes non-const references
        writer(copy);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kMeshMagic, kMeshVersion);
    writer(payloadHash, payload);
    return file;
}

Result<TriangleMesh> ReadMeshAsset(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kMeshMagic, kMeshVersion));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "mesh payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kPayloadMagic, kMeshVersion));
    TriangleMesh mesh;
    reader(mesh);
    INTERI_TRY(reader.Finish());
    INTERI_TRY(ValidateMesh(mesh));
    return mesh;
}

Result<TriangleMesh> LoadMeshAsset(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto mesh = ReadMeshAsset(bytes);
    if (!mesh)
    {
        return MakeError(mesh.error().code, std::format("{}: {}", path, mesh.error().message));
    }
    return mesh;
}

void RegisterMeshReflection(values::ReflectionRegistry& registry)
{
    registry.Register<MeshComponent>({
        values::Field("asset", &MeshComponent::asset),
        values::Field("colors", &MeshComponent::colors),
    });
}

values::Color SlotColor(const values::ValueList& colors, std::uint32_t slot)
{
    if (colors.items.empty())
    {
        return {.r = 0.6f, .g = 0.6f, .b = 0.6f, .a = 1.0f};
    }
    const std::size_t index = std::min<std::size_t>(slot, colors.items.size() - 1);
    const auto* color = std::get_if<values::Color>(&colors.items[index].Variant());
    return color == nullptr ? values::Color{.r = 0.6f, .g = 0.6f, .b = 0.6f, .a = 1.0f} : *color;
}

} // namespace interi::scene
