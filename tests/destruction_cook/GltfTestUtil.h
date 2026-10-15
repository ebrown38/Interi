// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace interi::destruction::testing
{

/// A box node for WriteBoxesGltf: local box [min, max], placed by `translation` and a uniform/axis `scale`.
struct GltfBox
{
    std::string name;
    Vec3 min;
    Vec3 max;
    int material = -1; ///< glTF material index, or -1 for none.
    Vec3 translation{};
    Vec3 scale{1, 1, 1};
};

inline std::string Base64(std::span<const std::uint8_t> bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::uint32_t b0 = bytes[i];
        const std::uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
        const std::uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out += kAlphabet[(triple >> 18) & 63];
        out += kAlphabet[(triple >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(triple >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[triple & 63] : '=';
    }
    return out;
}

/// Writes a .gltf (embedded base64 buffer) with one node per box: flat normals, uint32 indices. `materials` are the
/// glTF material names.
inline void WriteBoxesGltf(const std::filesystem::path& path, std::span<const GltfBox> boxes,
                           std::span<const std::string> materials = {})
{
    struct Face
    {
        std::array<float, 3> normal;
        std::array<int, 4> corners;
    };
    constexpr std::array<Face, 6> kFaces{{{{-1, 0, 0}, {0, 4, 6, 2}},
                                          {{1, 0, 0}, {1, 3, 7, 5}},
                                          {{0, -1, 0}, {0, 1, 5, 4}},
                                          {{0, 1, 0}, {2, 6, 7, 3}},
                                          {{0, 0, -1}, {0, 2, 3, 1}},
                                          {{0, 0, 1}, {4, 5, 7, 6}}}};
    std::vector<std::uint8_t> buffer;
    const auto append = [&](const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
    };

    std::string accessors;
    std::string views;
    std::string meshes;
    std::string nodes;
    std::string sceneNodes;
    for (std::size_t b = 0; b < boxes.size(); ++b)
    {
        const GltfBox& box = boxes[b];
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<std::uint32_t> indices;
        for (const Face& face : kFaces)
        {
            const auto base = static_cast<std::uint32_t>(positions.size() / 3);
            for (const int c : face.corners)
            {
                positions.insert(positions.end(),
                                 {(c & 1) != 0 ? box.max.x : box.min.x, (c & 2) != 0 ? box.max.y : box.min.y,
                                  (c & 4) != 0 ? box.max.z : box.min.z});
                normals.insert(normals.end(), face.normal.begin(), face.normal.end());
            }
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        const std::size_t firstView = 3 * b;
        const std::size_t offsets[3] = {buffer.size(), buffer.size() + positions.size() * 4,
                                        buffer.size() + positions.size() * 8};
        append(positions.data(), positions.size() * 4);
        append(normals.data(), normals.size() * 4);
        append(indices.data(), indices.size() * 4);
        const std::size_t lengths[3] = {positions.size() * 4, normals.size() * 4, indices.size() * 4};
        for (std::size_t v = 0; v < 3; ++v)
        {
            views += std::format("{}{{\"buffer\":0,\"byteOffset\":{},\"byteLength\":{}}}", views.empty() ? "" : ",",
                                 offsets[v], lengths[v]);
        }
        accessors += std::format(
            "{}{{\"bufferView\":{},\"componentType\":5126,\"count\":24,\"type\":\"VEC3\",\"min\":[{},{},{}],"
            "\"max\":[{},{},{}]}},{{\"bufferView\":{},\"componentType\":5126,\"count\":24,\"type\":\"VEC3\"}},"
            "{{\"bufferView\":{},\"componentType\":5125,\"count\":36,\"type\":\"SCALAR\"}}",
            accessors.empty() ? "" : ",", firstView, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
            firstView + 1, firstView + 2);
        meshes +=
            std::format("{}{{\"primitives\":[{{\"attributes\":{{\"POSITION\":{},\"NORMAL\":{}}},\"indices\":{}{}}}]}}",
                        meshes.empty() ? "" : ",", firstView, firstView + 1, firstView + 2,
                        box.material >= 0 ? std::format(",\"material\":{}", box.material) : "");
        nodes += std::format("{}{{\"name\":\"{}\",\"mesh\":{},\"translation\":[{},{},{}],\"scale\":[{},{},{}]}}",
                             nodes.empty() ? "" : ",", box.name, b, box.translation.x, box.translation.y,
                             box.translation.z, box.scale.x, box.scale.y, box.scale.z);
        sceneNodes += std::format("{}{}", sceneNodes.empty() ? "" : ",", b);
    }
    std::string materialList;
    for (const std::string& name : materials)
    {
        materialList += std::format("{}{{\"name\":\"{}\"}}", materialList.empty() ? "" : ",", name);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << std::format("{{\"asset\":{{\"version\":\"2.0\"}},\"scene\":0,\"scenes\":[{{\"nodes\":[{}]}}],"
                        "\"nodes\":[{}],\"meshes\":[{}]{},\"accessors\":[{}],\"bufferViews\":[{}],"
                        "\"buffers\":[{{\"byteLength\":{},\"uri\":\"data:application/octet-stream;base64,{}\"}}]}}",
                        sceneNodes, nodes, meshes,
                        materialList.empty() ? "" : std::format(",\"materials\":[{}]", materialList), accessors, views,
                        buffer.size(), Base64(buffer));
}

} // namespace interi::destruction::testing
