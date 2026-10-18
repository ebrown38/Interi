// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/modeling/Csg.h>

#include <manifold/manifold.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <vector>

namespace interi::modeling
{

namespace
{

struct Input
{
    manifold::Manifold solid;
    std::uint32_t slotBase = 0;
    std::uint32_t originalId = 0;
};

Result<Input> ToManifold(const CsgOperand& operand, std::size_t index, std::uint32_t slotBase)
{
    const scene::TriangleMesh& mesh = *operand.mesh;
    if (!scene::IsClosedManifold(mesh))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("csg: operand {} is not a closed mesh", index));
    }
    const Vec3 scale = operand.transform.scale;
    const bool mirrored = scale.x * scale.y * scale.z < 0.0f; // a mirror turns the triangles inside out
    manifold::MeshGL gl;
    gl.numProp = 3;
    gl.vertProperties.reserve(mesh.positions.size() * 3);
    for (const Vec3& position : mesh.positions)
    {
        const Vec3 world = operand.transform.TransformPoint(position);
        gl.vertProperties.insert(gl.vertProperties.end(), {world.x, world.y, world.z});
    }
    gl.triVerts.reserve(mesh.indices.size());
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const std::uint32_t a = mesh.indices[3 * t];
        const std::uint32_t b = mesh.indices[3 * t + 1];
        const std::uint32_t c = mesh.indices[3 * t + 2];
        gl.triVerts.insert(gl.triVerts.end(), {a, mirrored ? c : b, mirrored ? b : c});
    }
    manifold::Manifold solid(gl);
    if (solid.Status() != manifold::Manifold::Error::NoError)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("csg: operand {} is not a valid solid (Manifold error {})", index,
                                     static_cast<int>(solid.Status())));
    }
    solid = solid.AsOriginal();
    return Input{.solid = solid, .slotBase = slotBase, .originalId = static_cast<std::uint32_t>(solid.OriginalID())};
}

} // namespace

std::string_view ToString(CsgOperation operation) noexcept
{
    switch (operation)
    {
    case CsgOperation::Union:
        return "Union";
    case CsgOperation::Subtract:
        return "Subtract";
    case CsgOperation::Intersect:
        return "Intersect";
    }
    return "?";
}

Result<scene::TriangleMesh> Combine(std::span<const CsgOperand> operands, CsgOperation operation)
{
    if (operands.size() < 2 || std::ranges::any_of(operands, [](const CsgOperand& o) { return o.mesh == nullptr; }))
    {
        return MakeError(ErrorCode::InvalidArgument, "csg: needs at least two meshes");
    }
    std::vector<Input> inputs;
    std::uint32_t slotBase = 0;
    for (std::size_t i = 0; i < operands.size(); ++i)
    {
        INTERI_TRY_ASSIGN(input, ToManifold(operands[i], i, slotBase));
        slotBase += std::max<std::uint32_t>(operands[i].mesh->SlotCount(), 1);
        inputs.push_back(std::move(input));
    }
    manifold::Manifold result = inputs[0].solid;
    for (std::size_t i = 1; i < inputs.size(); ++i)
    {
        switch (operation)
        {
        case CsgOperation::Union:
            result = result + inputs[i].solid;
            break;
        case CsgOperation::Subtract:
            result = result - inputs[i].solid;
            break;
        case CsgOperation::Intersect:
            result = result ^ inputs[i].solid;
            break;
        }
    }
    if (result.Status() != manifold::Manifold::Error::NoError || result.IsEmpty())
    {
        return MakeError(ErrorCode::Degenerate, std::format("csg: the {} is empty", ToString(operation)));
    }

    const manifold::MeshGL gl = result.GetMeshGL();
    scene::TriangleMesh mesh;
    mesh.positions.reserve(gl.NumVert());
    for (std::size_t v = 0; v < gl.NumVert(); ++v)
    {
        mesh.positions.emplace_back(gl.vertProperties[v * gl.numProp], gl.vertProperties[v * gl.numProp + 1],
                                    gl.vertProperties[v * gl.numProp + 2]);
    }
    // Each run of triangles comes from one input (runOriginalID); within it, faceID is the input's triangle.
    std::map<std::uint32_t, const Input*> byOriginal;
    for (const Input& input : inputs)
    {
        byOriginal.emplace(input.originalId, &input);
    }
    for (std::size_t run = 0; run + 1 < gl.runIndex.size(); ++run)
    {
        const Input* input = byOriginal.at(gl.runOriginalID[run]);
        const auto operandIndex = static_cast<std::size_t>(input - inputs.data());
        const scene::TriangleMesh& source = *operands[operandIndex].mesh;
        for (std::size_t corner = gl.runIndex[run]; corner < gl.runIndex[run + 1]; corner += 3)
        {
            const std::size_t triangle = corner / 3;
            const std::uint32_t a = gl.triVerts[corner];
            const std::uint32_t b = gl.triVerts[corner + 1];
            const std::uint32_t c = gl.triVerts[corner + 2];
            const Vec3 cross = Cross(mesh.positions[b] - mesh.positions[a], mesh.positions[c] - mesh.positions[a]);
            const float length = Length(cross);
            const Vec3 normal = length > 0.0f ? cross / length : Vec3(0.0f, 1.0f, 0.0f);
            const std::uint32_t face = gl.faceID.empty() ? 0 : gl.faceID[triangle];
            const std::uint32_t slot = face < source.slots.size() ? source.slots[face] : 0;
            mesh.indices.insert(mesh.indices.end(), {a, b, c});
            mesh.normals.insert(mesh.normals.end(), {normal, normal, normal});
            mesh.slots.push_back(input->slotBase + slot);
        }
    }
    return mesh;
}

} // namespace interi::modeling
