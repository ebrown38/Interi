// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/destruction_cook/GltfImport.h>

#include <cgltf.h>

#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>

namespace interi::destruction
{
namespace
{

struct CgltfDeleter
{
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};
using CgltfData = std::unique_ptr<cgltf_data, CgltfDeleter>;

/// Column-major 4x4 world matrix in double precision.
struct WorldTransform
{
    std::array<double, 16> m{};

    [[nodiscard]] Vec3 Point(const std::array<float, 3>& p) const noexcept
    {
        std::array<double, 3> r{};
        for (std::size_t row = 0; row < 3; ++row)
        {
            r[row] = m[row] * static_cast<double>(p[0]) + m[4 + row] * static_cast<double>(p[1]) +
                     m[8 + row] * static_cast<double>(p[2]) + m[12 + row];
        }
        return {static_cast<float>(r[0]), static_cast<float>(r[1]), static_cast<float>(r[2])};
    }

    /// Normals transform by the cofactor matrix of the upper 3x3 (the inverse transpose times the determinant).
    [[nodiscard]] Vec3 Normal(const std::array<float, 3>& n) const noexcept
    {
        const auto a = [&](std::size_t row, std::size_t col) { return m[col * 4 + row]; };
        std::array<std::array<double, 3>, 3> cofactor{};
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t col = 0; col < 3; ++col)
            {
                const std::size_t r0 = (row + 1) % 3;
                const std::size_t r1 = (row + 2) % 3;
                const std::size_t c0 = (col + 1) % 3;
                const std::size_t c1 = (col + 2) % 3;
                cofactor[row][col] = a(r0, c0) * a(r1, c1) - a(r0, c1) * a(r1, c0);
            }
        }
        const double sign = Determinant() < 0.0 ? -1.0 : 1.0; // cofactor = det * inverse transpose
        std::array<double, 3> r{};
        for (std::size_t row = 0; row < 3; ++row)
        {
            r[row] =
                sign * (cofactor[row][0] * static_cast<double>(n[0]) + cofactor[row][1] * static_cast<double>(n[1]) +
                        cofactor[row][2] * static_cast<double>(n[2]));
        }
        return NormalizeOrZero(Vec3(static_cast<float>(r[0]), static_cast<float>(r[1]), static_cast<float>(r[2])));
    }

    [[nodiscard]] double Determinant() const noexcept
    {
        const auto a = [&](std::size_t row, std::size_t col) { return m[col * 4 + row]; };
        return a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) - a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0)) +
               a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
    }
};

const cgltf_accessor* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index)
{
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
    {
        if (primitive.attributes[i].type == type && primitive.attributes[i].index == index)
        {
            return primitive.attributes[i].data;
        }
    }
    return nullptr;
}

template <std::size_t N>
std::array<float, N> ReadFloats(const cgltf_accessor* accessor, cgltf_size index)
{
    std::array<float, N> out{};
    if (accessor != nullptr)
    {
        cgltf_accessor_read_float(accessor, index, out.data(), N);
    }
    return out;
}

void AppendPrimitive(const cgltf_primitive& primitive, const WorldTransform& world, std::uint32_t surface,
                     FractureMesh& mesh)
{
    const cgltf_accessor* positions = FindAttribute(primitive, cgltf_attribute_type_position, 0);
    if (primitive.type != cgltf_primitive_type_triangles || positions == nullptr)
    {
        return;
    }
    const cgltf_accessor* normals = FindAttribute(primitive, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* uvs = FindAttribute(primitive, cgltf_attribute_type_texcoord, 0);
    const cgltf_size count = primitive.indices != nullptr ? primitive.indices->count : positions->count;
    const bool mirrored = world.Determinant() < 0.0;

    for (cgltf_size t = 0; t + 2 < count; t += 3)
    {
        FractureTriangle triangle;
        triangle.surfaceMaterial = surface;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const cgltf_size corner = t + (mirrored ? 2 - k : k); // a mirroring transform flips the winding
            const cgltf_size vertex =
                primitive.indices != nullptr ? cgltf_accessor_read_index(primitive.indices, corner) : corner;
            triangle.indices[k] = static_cast<std::uint32_t>(mesh.positions.size());
            mesh.positions.push_back(world.Point(ReadFloats<3>(positions, vertex)));
            triangle.normals[k] = normals != nullptr ? world.Normal(ReadFloats<3>(normals, vertex)) : Vec3();
            const auto uv = ReadFloats<2>(uvs, vertex);
            triangle.uvs[k] = {uv[0], uv[1]};
        }
        if (normals == nullptr)
        {
            const Vec3& a = mesh.positions[triangle.indices[0]];
            const Vec3 flat = NormalizeOrZero(
                Cross(mesh.positions[triangle.indices[1]] - a, mesh.positions[triangle.indices[2]] - a));
            triangle.normals = {flat, flat, flat};
        }
        mesh.triangles.push_back(triangle);
    }
}

} // namespace

Result<ImportedScene> ImportGltf(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot open '{}'", path.string()));
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    const cgltf_options options{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &raw) != cgltf_result_success)
    {
        return MakeError(ErrorCode::Corrupt, std::format("'{}' is not a valid glTF file", path.string()));
    }
    const CgltfData data(raw);
    const std::string pathString = path.string();
    if (cgltf_load_buffers(&options, data.get(), pathString.c_str()) != cgltf_result_success)
    {
        return MakeError(ErrorCode::Corrupt, std::format("'{}': cannot load its buffers", pathString));
    }
    if (cgltf_validate(data.get()) != cgltf_result_success)
    {
        return MakeError(ErrorCode::Corrupt, std::format("'{}' failed glTF validation", pathString));
    }

    ImportedScene scene;
    scene.contentHash = HashBytes(bytes.data(), bytes.size());
    for (cgltf_size b = 0; b < data->buffers_count; ++b)
    {
        scene.contentHash = HashCombine(scene.contentHash, HashBytes(data->buffers[b].data, data->buffers[b].size));
    }
    for (cgltf_size m = 0; m < data->materials_count; ++m)
    {
        const char* name = data->materials[m].name;
        scene.surfaceMaterials.emplace_back(name != nullptr ? name : std::format("material{}", m));
    }
    scene.surfaceMaterials.emplace_back("default");
    const auto defaultSurface = static_cast<std::uint32_t>(data->materials_count);

    for (cgltf_size n = 0; n < data->nodes_count; ++n)
    {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh == nullptr)
        {
            continue;
        }
        WorldTransform world;
        std::array<float, 16> matrix{};
        cgltf_node_transform_world(&node, matrix.data());
        for (std::size_t i = 0; i < 16; ++i)
        {
            world.m[i] = static_cast<double>(matrix[i]);
        }

        SourceObject object;
        object.name = node.name != nullptr ? node.name : std::format("node{}", n);
        FractureMesh unwelded;
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
        {
            const cgltf_primitive& primitive = node.mesh->primitives[p];
            const auto surface = primitive.material != nullptr
                                     ? static_cast<std::uint32_t>(primitive.material - data->materials)
                                     : defaultSurface;
            if (p == 0 && primitive.material != nullptr)
            {
                object.firstMaterialName = scene.surfaceMaterials[surface];
            }
            AppendPrimitive(primitive, world, surface, unwelded);
        }
        if (unwelded.triangles.empty())
        {
            continue;
        }
        object.mesh = WeldIdenticalPositions(unwelded);
        scene.objects.push_back(std::move(object));
    }
    if (scene.objects.empty())
    {
        return MakeError(ErrorCode::Unsupported, std::format("'{}' has no triangle meshes", pathString));
    }
    return scene;
}

} // namespace interi::destruction
