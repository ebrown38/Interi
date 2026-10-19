// SPDX-License-Identifier: MIT
#include "ModelProcess.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <numbers>
#include <unordered_map>

namespace interi::asset_import
{

namespace
{

constexpr std::size_t kSmoothingValenceLimit = 64;

struct PositionKey
{
    std::array<std::uint32_t, 3> bits{};

    friend bool operator==(const PositionKey&, const PositionKey&) = default;
};

struct PositionKeyHash
{
    std::size_t operator()(const PositionKey& key) const noexcept
    {
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (const std::uint32_t b : key.bits)
        {
            h = (h ^ b) * 0x100000001b3ULL;
        }
        return static_cast<std::size_t>(h);
    }
};

[[nodiscard]] PositionKey KeyOf(const Vec3& p) noexcept
{
    // -0 and +0 are the same position.
    const auto bits = [](float v)
    {
        if (v == 0.0f)
        {
            v = 0.0f;
        }
        std::uint32_t result = 0;
        std::memcpy(&result, &v, sizeof(result));
        return result;
    };
    return {{bits(p.x), bits(p.y), bits(p.z)}};
}

[[nodiscard]] bool Finite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] Vec3 Linear(const Mat4& m, const Vec3& v) noexcept
{
    return TransformVector(m, v);
}

/// Inverse transpose of the upper 3x3 of `m` as a Mat4 (for normals).
[[nodiscard]] Mat4 NormalMatrix(const Mat4& m) noexcept
{
    Mat4 linear = m;
    linear.columns[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return Transpose(Inverse(linear));
}

template <typename T>
void Gather(std::vector<T>& values, const std::vector<std::size_t>& source)
{
    if (values.empty())
    {
        return;
    }
    std::vector<T> result;
    result.reserve(source.size());
    for (const std::size_t index : source)
    {
        result.push_back(values[index]);
    }
    values = std::move(result);
}

} // namespace

Mat4 SourceToEngine(const ModelImportSettings& settings)
{
    Mat4 handed = Mat4::Identity();
    if (settings.handedness == Handedness::Left)
    {
        // Mirror an axis that is not the up axis.
        handed = Mat4::Scale(settings.up == UpAxis::Y ? Vec3(1.0f, 1.0f, -1.0f) : Vec3(1.0f, -1.0f, 1.0f));
    }
    Mat4 up = Mat4::Identity();
    if (settings.up == UpAxis::Z)
    {
        up = Mat4{{{1, 0, 0, 0}, {0, 0, -1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}}}; // (x, y, z) -> (x, z, -y)
    }
    return Mat4::Scale(Vec3(settings.scale)) * up * handed;
}

assets::JointTransform Decompose(const Mat4& matrix)
{
    assets::JointTransform result;
    result.translation = GetTranslation(matrix);
    Vec3 axes[3] = {matrix.columns[0].XYZ(), matrix.columns[1].XYZ(), matrix.columns[2].XYZ()};
    Vec3 scale(Length(axes[0]), Length(axes[1]), Length(axes[2]));
    if (Determinant(matrix) < 0.0f)
    {
        scale.x = -scale.x;
    }
    const auto unit = [](const Vec3& axis, float length) { return length > 1e-20f ? axis / length : axis; };
    Mat4 rotation = Mat4::Identity();
    rotation.columns[0] = Vec4(unit(axes[0], scale.x), 0.0f);
    rotation.columns[1] = Vec4(unit(axes[1], scale.y), 0.0f);
    rotation.columns[2] = Vec4(unit(axes[2], scale.z), 0.0f);
    result.rotation = Normalize(Quat::FromRotationMatrix(rotation));
    result.scale = scale;
    return result;
}

void ConvertSkeleton(assets::Skeleton& skeleton, const Mat4& conversion)
{
    if (conversion.columns[0] == Mat4::Identity().columns[0] && conversion.columns[1] == Mat4::Identity().columns[1] &&
        conversion.columns[2] == Mat4::Identity().columns[2] && conversion.columns[3] == Mat4::Identity().columns[3])
    {
        return;
    }
    const Mat4 inverse = Inverse(conversion);
    std::vector<Mat4> world(skeleton.joints.size());
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
    {
        assets::Joint& joint = skeleton.joints[i];
        const assets::JointTransform& local = joint.bind;
        const Mat4 localMatrix = Mat4::FromTRS(local.translation, local.rotation, local.scale);
        const Mat4 source =
            joint.parent >= 0 ? world[static_cast<std::size_t>(joint.parent)] * localMatrix : localMatrix;
        world[i] = source;
    }
    std::vector<Mat4> converted(world.size());
    for (std::size_t i = 0; i < world.size(); ++i)
    {
        converted[i] = conversion * world[i] * inverse;
        assets::Joint& joint = skeleton.joints[i];
        joint.inverseBind = conversion * joint.inverseBind * inverse;
        const Mat4 local = joint.parent >= 0 ? Inverse(converted[static_cast<std::size_t>(joint.parent)]) * converted[i]
                                             : converted[i];
        joint.bind = Decompose(local);
    }
}

void ConvertClip(assets::AnimationClip& clip, const Mat4& conversion)
{
    Mat4 linear = conversion;
    linear.columns[3] = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const std::optional<Mat4> inverse = TryInverse(linear);
    if (!inverse)
    {
        return;
    }
    // The normalized linear part (rotation or reflection) acts on rotations and scale axes.
    Mat4 normalized = linear;
    for (int i = 0; i < 3; ++i)
    {
        const Vec3 axis = linear.columns[i].XYZ();
        normalized.columns[i] = Vec4(NormalizeOrZero(axis), 0.0f);
    }
    const Mat4 normalizedInverse = Inverse(normalized);
    for (assets::AnimationChannel& channel : clip.channels)
    {
        for (Vec4& value : channel.values)
        {
            switch (channel.path)
            {
            case assets::ChannelPath::Translation:
                value = Vec4(Linear(linear, value.XYZ()), 0.0f);
                break;
            case assets::ChannelPath::Scale:
                value = Vec4(Abs(Linear(normalized, value.XYZ())), 0.0f);
                break;
            case assets::ChannelPath::Rotation:
            {
                const Quat q = Normalize(Quat{value.x, value.y, value.z, value.w});
                const Mat4 rotated = normalized * Mat4::Rotation(q) * normalizedInverse;
                const Quat result = Normalize(Quat::FromRotationMatrix(rotated));
                value = Vec4(result.x, result.y, result.z, result.w);
                break;
            }
            }
        }
    }
}

Result<ImportedMesh> BuildMesh(CornerMesh corners, const Mat4& transform, const ModelImportSettings& settings,
                               std::vector<std::string>& warnings)
{
    const auto corrupt = [&](std::string message)
    { return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': {}", corners.name, std::move(message))); };
    const std::size_t cornerCount = corners.positions.size();
    if (cornerCount % 3 != 0 || corners.materials.size() != cornerCount / 3)
    {
        return corrupt("corner and triangle counts disagree");
    }
    if (cornerCount / 3 > kMaxImportTriangles)
    {
        return corrupt("too many triangles");
    }
    const auto sized = [&](std::size_t n) { return n == 0 || n == cornerCount; };
    if (!sized(corners.normals.size()) || !sized(corners.uvs.size()) || !sized(corners.tangents.size()) ||
        !sized(corners.skin.size()))
    {
        return corrupt("attribute counts disagree");
    }
    const std::optional<Mat4> inverseTransform = TryInverse(transform);
    if (!inverseTransform)
    {
        return corrupt("degenerate transform");
    }
    const bool mirrored = Determinant(transform) < 0.0f;
    const Mat4 normalMatrix = NormalMatrix(transform);

    // 1. Place the corners.
    for (std::size_t i = 0; i < cornerCount; ++i)
    {
        corners.positions[i] = TransformPoint(transform, corners.positions[i]);
        if (!Finite(corners.positions[i]))
        {
            return corrupt("non-finite position");
        }
    }
    for (Vec3& normal : corners.normals)
    {
        normal = NormalizeOrZero(Linear(normalMatrix, normal));
        if (!Finite(normal))
        {
            return corrupt("non-finite normal");
        }
    }
    for (Vec4& tangent : corners.tangents)
    {
        const Vec3 direction = NormalizeOrZero(Linear(transform, tangent.XYZ()));
        tangent = Vec4(direction, mirrored ? -tangent.w : tangent.w);
        if (!Finite(direction) || !std::isfinite(tangent.w))
        {
            return corrupt("non-finite tangent");
        }
    }
    for (const scene::TexCoord& uv : corners.uvs)
    {
        if (!std::isfinite(uv.u) || !std::isfinite(uv.v))
        {
            return corrupt("non-finite texture coordinate");
        }
    }
    if (mirrored)
    {
        // A reflection reverses winding: swap the last two corners of every triangle.
        for (std::size_t t = 0; t < cornerCount / 3; ++t)
        {
            const auto swap = [&](auto& values)
            {
                if (!values.empty())
                {
                    std::swap(values[3 * t + 1], values[3 * t + 2]);
                }
            };
            swap(corners.positions);
            swap(corners.normals);
            swap(corners.uvs);
            swap(corners.tangents);
            swap(corners.skin);
        }
    }

    // 2. Weld positions (first appearance order) and drop triangles that collapsed.
    std::vector<Vec3> positions;
    std::vector<std::uint32_t> cornerVertex(cornerCount);
    {
        std::unordered_map<PositionKey, std::uint32_t, PositionKeyHash> welded;
        welded.reserve(cornerCount);
        for (std::size_t i = 0; i < cornerCount; ++i)
        {
            const auto [it, inserted] =
                welded.try_emplace(KeyOf(corners.positions[i]), static_cast<std::uint32_t>(positions.size()));
            if (inserted)
            {
                positions.push_back(corners.positions[i]);
            }
            cornerVertex[i] = it->second;
        }
    }
    std::vector<std::size_t> keptCorners;
    keptCorners.reserve(cornerCount);
    std::vector<std::int32_t> triangleMaterials;
    std::size_t dropped = 0;
    for (std::size_t t = 0; t < cornerCount / 3; ++t)
    {
        const std::uint32_t a = cornerVertex[3 * t];
        const std::uint32_t b = cornerVertex[3 * t + 1];
        const std::uint32_t c = cornerVertex[3 * t + 2];
        if (a == b || b == c || a == c)
        {
            ++dropped;
            continue;
        }
        for (std::size_t k = 0; k < 3; ++k)
        {
            keptCorners.push_back(3 * t + k);
        }
        triangleMaterials.push_back(corners.materials[t]);
    }
    if (dropped > 0)
    {
        warnings.push_back(std::format("mesh '{}': dropped {} degenerate triangle(s)", corners.name, dropped));
    }
    Gather(corners.normals, keptCorners);
    Gather(corners.uvs, keptCorners);
    Gather(corners.tangents, keptCorners);
    Gather(corners.skin, keptCorners);
    std::vector<std::uint32_t> indices;
    indices.reserve(keptCorners.size());
    for (const std::size_t corner : keptCorners)
    {
        indices.push_back(cornerVertex[corner]);
    }
    const std::size_t triangleCount = indices.size() / 3;

    // 3. Slots: distinct materials in order of first appearance.
    ImportedMesh result;
    result.name = corners.name;
    result.skeleton = corners.skeleton;
    std::vector<std::uint32_t> slots(triangleCount);
    for (std::size_t t = 0; t < triangleCount; ++t)
    {
        const auto found = std::ranges::find(result.slotMaterials, triangleMaterials[t]);
        if (found == result.slotMaterials.end())
        {
            result.slotMaterials.push_back(triangleMaterials[t]);
            slots[t] = static_cast<std::uint32_t>(result.slotMaterials.size() - 1);
        }
        else
        {
            slots[t] = static_cast<std::uint32_t>(found - result.slotMaterials.begin());
        }
    }

    // 4. Normals.
    std::vector<Vec3> faceNormals(triangleCount);
    for (std::size_t t = 0; t < triangleCount; ++t)
    {
        const Vec3& p0 = positions[indices[3 * t]];
        faceNormals[t] = Cross(positions[indices[3 * t + 1]] - p0, positions[indices[3 * t + 2]] - p0);
    }
    NormalMode mode = settings.normals;
    if (mode == NormalMode::FromFile && corners.normals.empty())
    {
        mode = NormalMode::Smooth;
    }
    if (mode != NormalMode::FromFile)
    {
        corners.normals.assign(indices.size(), Vec3(0.0f, 1.0f, 0.0f));
        const auto faceDirection = [&](std::size_t t) { return NormalizeOrZero(faceNormals[t]); };
        if (mode == NormalMode::Flat)
        {
            for (std::size_t t = 0; t < triangleCount; ++t)
            {
                const Vec3 n = faceDirection(t);
                for (std::size_t k = 0; k < 3; ++k)
                {
                    corners.normals[3 * t + k] = n == Vec3(0.0f) ? Vec3(0.0f, 1.0f, 0.0f) : n;
                }
            }
        }
        else
        {
            std::vector<std::vector<std::uint32_t>> around(positions.size());
            for (std::size_t t = 0; t < triangleCount; ++t)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    around[indices[3 * t + k]].push_back(static_cast<std::uint32_t>(t));
                }
            }
            // Angle weighting makes the result independent of how quads were split into triangles.
            std::vector<float> cornerAngle(indices.size());
            for (std::size_t t = 0; t < triangleCount; ++t)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const Vec3& p = positions[indices[3 * t + k]];
                    const Vec3 e1 = positions[indices[3 * t + (k + 1) % 3]] - p;
                    const Vec3 e2 = positions[indices[3 * t + (k + 2) % 3]] - p;
                    cornerAngle[3 * t + k] = std::atan2(Length(Cross(e1, e2)), Dot(e1, e2));
                }
            }
            const auto angleAt = [&](std::size_t triangle, std::uint32_t vertex)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    if (indices[3 * triangle + k] == vertex)
                    {
                        return cornerAngle[3 * triangle + k];
                    }
                }
                return 0.0f;
            };
            const float limit = std::cos(settings.smoothingAngleDegrees * std::numbers::pi_v<float> / 180.0f);
            for (std::size_t t = 0; t < triangleCount; ++t)
            {
                const Vec3 own = faceDirection(t);
                for (std::size_t k = 0; k < 3; ++k)
                {
                    const std::uint32_t vertex = indices[3 * t + k];
                    const auto& neighbours = around[vertex];
                    Vec3 sum = own * cornerAngle[3 * t + k];
                    for (const std::uint32_t u : neighbours)
                    {
                        if (u == t)
                        {
                            continue;
                        }
                        const Vec3 other = faceDirection(u);
                        if (neighbours.size() > kSmoothingValenceLimit || Dot(own, other) >= limit)
                        {
                            sum = sum + other * angleAt(u, vertex);
                        }
                    }
                    const Vec3 n = NormalizeOrZero(sum);
                    corners.normals[3 * t + k] = n == Vec3(0.0f) ? Vec3(0.0f, 1.0f, 0.0f) : n;
                }
            }
        }
    }

    // 5. Tangents: from the UVs when the file gave none.
    if (settings.generateTangents && !corners.uvs.empty() && corners.tangents.empty())
    {
        struct Accum
        {
            Vec3 tangent{0.0f};
            Vec3 bitangent{0.0f};
        };
        using Key = std::array<std::uint32_t, 3>;
        std::map<Key, Accum> sums;
        const auto keyOf = [&](std::size_t corner)
        {
            std::uint32_t u = 0;
            std::uint32_t v = 0;
            std::memcpy(&u, &corners.uvs[corner].u, sizeof(u));
            std::memcpy(&v, &corners.uvs[corner].v, sizeof(v));
            return Key{indices[corner], u, v};
        };
        for (std::size_t t = 0; t < triangleCount; ++t)
        {
            const Vec3& p0 = positions[indices[3 * t]];
            const Vec3 e1 = positions[indices[3 * t + 1]] - p0;
            const Vec3 e2 = positions[indices[3 * t + 2]] - p0;
            const float du1 = corners.uvs[3 * t + 1].u - corners.uvs[3 * t].u;
            const float dv1 = corners.uvs[3 * t + 1].v - corners.uvs[3 * t].v;
            const float du2 = corners.uvs[3 * t + 2].u - corners.uvs[3 * t].u;
            const float dv2 = corners.uvs[3 * t + 2].v - corners.uvs[3 * t].v;
            const float determinant = du1 * dv2 - du2 * dv1;
            if (std::abs(determinant) < 1e-20f)
            {
                continue;
            }
            const float r = 1.0f / determinant;
            const Vec3 tangent = (e1 * dv2 - e2 * dv1) * r;
            const Vec3 towardsIncreasingV = (e2 * du1 - e1 * du2) * r;
            for (std::size_t k = 0; k < 3; ++k)
            {
                Accum& accum = sums[keyOf(3 * t + k)];
                accum.tangent = accum.tangent + tangent;
                accum.bitangent = accum.bitangent - towardsIncreasingV; // image "up" is decreasing v
            }
        }
        corners.tangents.resize(indices.size());
        for (std::size_t c = 0; c < indices.size(); ++c)
        {
            const Vec3& n = corners.normals[c];
            const auto it = sums.find(keyOf(c));
            Vec3 tangent = it == sums.end() ? Vec3(0.0f) : it->second.tangent;
            tangent = NormalizeOrZero(tangent - n * Dot(n, tangent));
            if (tangent == Vec3(0.0f))
            {
                // No usable UV mapping here: any direction perpendicular to the normal.
                tangent =
                    NormalizeOrZero(Cross(std::abs(n.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f), n));
            }
            const float sign = it != sums.end() && Dot(Cross(n, tangent), it->second.bitangent) < 0.0f ? -1.0f : 1.0f;
            corners.tangents[c] = Vec4(tangent, sign);
        }
    }

    // 6. meshoptimizer: triangle order for the vertex cache, vertex order for fetch.
    if (settings.optimize && triangleCount > 0)
    {
        std::vector<std::uint32_t> optimized(indices.size());
        meshopt_optimizeVertexCache(optimized.data(), indices.data(), indices.size(), positions.size());
        // Recover which original triangle (and corner order) each output triangle is.
        std::map<std::array<std::uint32_t, 3>, std::vector<std::size_t>> byVertices;
        for (std::size_t t = triangleCount; t-- > 0;)
        {
            std::array<std::uint32_t, 3> key = {indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]};
            std::ranges::sort(key);
            byVertices[key].push_back(t); // popped from the back: earliest first
        }
        std::vector<std::size_t> order(indices.size());
        std::vector<std::uint32_t> orderedSlots(triangleCount);
        for (std::size_t t = 0; t < triangleCount; ++t)
        {
            std::array<std::uint32_t, 3> key = {optimized[3 * t], optimized[3 * t + 1], optimized[3 * t + 2]};
            std::ranges::sort(key);
            auto& candidates = byVertices.at(key);
            const std::size_t original = candidates.back();
            candidates.pop_back();
            orderedSlots[t] = slots[original];
            for (std::size_t k = 0; k < 3; ++k)
            {
                std::size_t from = 3 * original;
                for (std::size_t j = 0; j < 3; ++j)
                {
                    if (indices[3 * original + j] == optimized[3 * t + k])
                    {
                        from = 3 * original + j;
                    }
                }
                order[3 * t + k] = from;
            }
        }
        Gather(corners.normals, order);
        Gather(corners.uvs, order);
        Gather(corners.tangents, order);
        Gather(corners.skin, order);
        slots = std::move(orderedSlots);
        std::vector<unsigned int> remap(positions.size());
        const std::size_t used =
            meshopt_optimizeVertexFetchRemap(remap.data(), optimized.data(), optimized.size(), positions.size());
        std::vector<Vec3> fetched(used);
        meshopt_remapVertexBuffer(fetched.data(), positions.data(), positions.size(), sizeof(Vec3), remap.data());
        std::vector<std::uint32_t> remapped(optimized.size());
        meshopt_remapIndexBuffer(remapped.data(), optimized.data(), optimized.size(), remap.data());
        positions = std::move(fetched);
        indices = std::move(remapped);
    }

    result.mesh.positions = std::move(positions);
    result.mesh.indices = std::move(indices);
    result.mesh.normals = std::move(corners.normals);
    result.mesh.slots = std::move(slots);
    result.mesh.uvs = std::move(corners.uvs);
    result.mesh.tangents = std::move(corners.tangents);
    result.skin = std::move(corners.skin);
    if (auto valid = scene::ValidateMesh(result.mesh); !valid)
    {
        return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': {}", corners.name, valid.error().message));
    }
    return result;
}

} // namespace interi::asset_import
