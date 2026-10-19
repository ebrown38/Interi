// SPDX-License-Identifier: MIT
#pragma once

// Source files for the importer tests, generated in code: a cube as glTF (data URI or GLB), OBJ and ASCII FBX, and
// small PNG, TGA and HDR images.

#include <interi/core/math/Vec.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace interi::testing_fixtures
{

inline std::vector<std::byte> Bytes(std::string_view text)
{
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

inline std::string Base64(const std::vector<std::byte>& bytes)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const std::uint32_t b0 = std::to_integer<std::uint32_t>(bytes[i]);
        const std::uint32_t b1 = i + 1 < bytes.size() ? std::to_integer<std::uint32_t>(bytes[i + 1]) : 0;
        const std::uint32_t b2 = i + 2 < bytes.size() ? std::to_integer<std::uint32_t>(bytes[i + 2]) : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(alphabet[(triple >> 18) & 63]);
        out.push_back(alphabet[(triple >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? alphabet[(triple >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? alphabet[triple & 63] : '=');
    }
    return out;
}

template <typename T>
void Append(std::vector<std::byte>& out, const T& value)
{
    const auto* p = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

// --- Cube -------------------------------------------------------------------------------------------------------

struct CubeData
{
    std::vector<Vec3> positions; ///< 24: four per face.
    std::vector<Vec3> normals;
    std::vector<std::array<float, 2>> uvs; ///< glTF convention (v down).
    std::vector<std::uint16_t> indices;    ///< 36.
};

/// A unit cube centred on the origin, counter-clockwise faces, four vertices per face.
inline CubeData MakeCube(float size = 1.0f)
{
    CubeData cube;
    struct Face
    {
        Vec3 n, u, v;
    };
    const Face faces[6] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                           {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
                           {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}};
    const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (const Face& face : faces)
    {
        const auto base = static_cast<std::uint16_t>(cube.positions.size());
        for (int c = 0; c < 4; ++c)
        {
            cube.positions.push_back((face.n + face.u * signs[c][0] + face.v * signs[c][1]) * (0.5f * size));
            cube.normals.push_back(face.n);
            cube.uvs.push_back({uv[c][0], uv[c][1]});
        }
        for (const std::uint16_t i : std::array<std::uint16_t, 6>{0, 1, 2, 0, 2, 3})
        {
            cube.indices.push_back(static_cast<std::uint16_t>(base + i));
        }
    }
    return cube;
}

struct GltfBuffer
{
    std::vector<std::byte> bytes;
    nlohmann::json bufferViews = nlohmann::json::array();
    nlohmann::json accessors = nlohmann::json::array();

    /// Appends `data` as a buffer view and an accessor; returns the accessor index.
    int Add(const std::vector<std::byte>& data, int componentType, std::size_t count, const char* type, int target = 0,
            const nlohmann::json& extra = {})
    {
        while (bytes.size() % 4 != 0)
        {
            bytes.push_back(std::byte{0});
        }
        nlohmann::json view = {{"buffer", 0}, {"byteOffset", bytes.size()}, {"byteLength", data.size()}};
        if (target != 0)
        {
            view["target"] = target;
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        bufferViews.push_back(view);
        nlohmann::json accessor = {
            {"bufferView", bufferViews.size() - 1}, {"componentType", componentType}, {"count", count}, {"type", type}};
        for (const auto& [key, value] : extra.items())
        {
            accessor[key] = value;
        }
        accessors.push_back(accessor);
        return static_cast<int>(accessors.size() - 1);
    }
};

template <typename T>
std::vector<std::byte> Pack(const std::vector<T>& values)
{
    std::vector<std::byte> out;
    for (const T& v : values)
    {
        Append(out, v);
    }
    return out;
}

inline std::vector<std::byte> PackVec3(const std::vector<Vec3>& values)
{
    std::vector<std::byte> out;
    for (const Vec3& v : values)
    {
        Append(out, v.x);
        Append(out, v.y);
        Append(out, v.z);
    }
    return out;
}

inline std::vector<std::byte> PackUv(const std::vector<std::array<float, 2>>& values)
{
    std::vector<std::byte> out;
    for (const auto& v : values)
    {
        Append(out, v[0]);
        Append(out, v[1]);
    }
    return out;
}

struct GltfOptions
{
    nlohmann::json nodeExtra = nlohmann::json::object(); ///< Merged into the mesh node (translation, scale...).
    nlohmann::json materials = nlohmann::json::array();
    nlohmann::json extraRoot = nlohmann::json::object(); ///< Merged into the document (images, textures...).
    bool externalBuffer = false;                         ///< The buffer is "cube.bin" instead of a data URI.
    bool glb = false;                                    ///< Produce a GLB, with the buffer as the BIN chunk.
    std::string bufferUri;                               ///< Overrides the external buffer's URI.
    float size = 1.0f;
    std::size_t indexOverride = 0; ///< If non-zero, the last index is replaced by this.
};

struct GltfFile
{
    std::vector<std::byte> main;
    std::vector<std::byte> bin; ///< External buffer contents (when options.externalBuffer).
};

inline GltfFile MakeCubeGltf(const GltfOptions& options = {})
{
    CubeData cube = MakeCube(options.size);
    if (options.indexOverride != 0)
    {
        cube.indices.back() = static_cast<std::uint16_t>(options.indexOverride);
    }
    GltfBuffer buffer;
    const int indices = buffer.Add(Pack(cube.indices), 5123, cube.indices.size(), "SCALAR", 34963);
    const int positions = buffer.Add(PackVec3(cube.positions), 5126, cube.positions.size(), "VEC3", 34962,
                                     {{"min", {-0.5, -0.5, -0.5}}, {"max", {0.5, 0.5, 0.5}}});
    const int normals = buffer.Add(PackVec3(cube.normals), 5126, cube.normals.size(), "VEC3", 34962);
    const int uvs = buffer.Add(PackUv(cube.uvs), 5126, cube.uvs.size(), "VEC2", 34962);

    nlohmann::json primitive = {{"attributes", {{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", uvs}}},
                                {"indices", indices}};
    if (!options.materials.empty())
    {
        primitive["material"] = 0;
    }
    nlohmann::json node = {{"mesh", 0}, {"name", "Cube"}};
    for (const auto& [key, value] : options.nodeExtra.items())
    {
        node[key] = value;
    }
    nlohmann::json doc = {{"asset", {{"version", "2.0"}}},
                          {"scene", 0},
                          {"scenes", {{{"nodes", {0}}}}},
                          {"nodes", {node}},
                          {"meshes", {{{"primitives", {primitive}}}}},
                          {"accessors", buffer.accessors},
                          {"bufferViews", buffer.bufferViews}};
    nlohmann::json bufferJson = {{"byteLength", buffer.bytes.size()}};
    GltfFile file;
    if (options.glb)
    {
        // no uri: the BIN chunk
    }
    else if (options.externalBuffer)
    {
        bufferJson["uri"] = options.bufferUri.empty() ? "cube.bin" : options.bufferUri;
        file.bin = buffer.bytes;
    }
    else
    {
        bufferJson["uri"] = "data:application/octet-stream;base64," + Base64(buffer.bytes);
    }
    doc["buffers"] = {bufferJson};
    if (!options.materials.empty())
    {
        doc["materials"] = options.materials;
    }
    for (const auto& [key, value] : options.extraRoot.items())
    {
        doc[key] = value;
    }
    const std::string json = doc.dump();
    if (!options.glb)
    {
        file.main = Bytes(json);
        return file;
    }
    std::string padded = json;
    while (padded.size() % 4 != 0)
    {
        padded.push_back(' ');
    }
    std::vector<std::byte> bin = buffer.bytes;
    while (bin.size() % 4 != 0)
    {
        bin.push_back(std::byte{0});
    }
    std::vector<std::byte> out;
    Append(out, std::uint32_t{0x46546C67}); // "glTF"
    Append(out, std::uint32_t{2});
    Append(out, static_cast<std::uint32_t>(12 + 8 + padded.size() + 8 + bin.size()));
    Append(out, static_cast<std::uint32_t>(padded.size()));
    Append(out, std::uint32_t{0x4E4F534A}); // "JSON"
    const auto jsonBytes = Bytes(padded);
    out.insert(out.end(), jsonBytes.begin(), jsonBytes.end());
    Append(out, static_cast<std::uint32_t>(bin.size()));
    Append(out, std::uint32_t{0x004E4942}); // "BIN\0"
    out.insert(out.end(), bin.begin(), bin.end());
    file.main = std::move(out);
    return file;
}

/// The cube as OBJ with quad faces, normals and UVs (OBJ convention: v up).
inline std::string MakeCubeObj(bool withNormals = true, bool withUvs = true, std::string_view extra = {})
{
    const CubeData cube = MakeCube();
    std::string text(extra);
    for (const Vec3& p : cube.positions)
    {
        text += "v " + std::to_string(p.x) + " " + std::to_string(p.y) + " " + std::to_string(p.z) + "\n";
    }
    if (withNormals)
    {
        for (const Vec3& n : cube.normals)
        {
            text += "vn " + std::to_string(n.x) + " " + std::to_string(n.y) + " " + std::to_string(n.z) + "\n";
        }
    }
    if (withUvs)
    {
        for (const auto& uv : cube.uvs)
        {
            text += "vt " + std::to_string(uv[0]) + " " + std::to_string(1.0f - uv[1]) + "\n";
        }
    }
    for (std::size_t f = 0; f < 6; ++f)
    {
        text += "f";
        for (std::size_t c = 0; c < 4; ++c)
        {
            const std::size_t i = f * 4 + c + 1;
            text += " " + std::to_string(i);
            if (withUvs || withNormals)
            {
                text +=
                    "/" + (withUvs ? std::to_string(i) : std::string()) + (withNormals ? "/" + std::to_string(i) : "");
            }
        }
        text += "\n";
    }
    return text;
}

/// The cube as an ASCII FBX 7.4 file (Y up, centimetres: a unit of 0.01 m, so the cube is 100 cm wide).
inline std::string MakeCubeFbx()
{
    const CubeData cube = MakeCube(100.0f);
    std::string vertices;
    for (const Vec3& p : cube.positions)
    {
        vertices += std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + ",";
    }
    vertices.pop_back();
    std::string polygons;
    for (std::size_t f = 0; f < 6; ++f)
    {
        const std::size_t b = f * 4;
        polygons += std::to_string(b) + "," + std::to_string(b + 1) + "," + std::to_string(b + 2) + "," +
                    std::to_string(-static_cast<int>(b + 3) - 1) + ",";
    }
    polygons.pop_back();
    std::string normals;
    for (const Vec3& n : cube.normals)
    {
        normals += std::to_string(n.x) + "," + std::to_string(n.y) + "," + std::to_string(n.z) + ",";
    }
    normals.pop_back();
    return std::string("; FBX 7.4.0 project file\n") +
           "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
           "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
           "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
           "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
           "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
           "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n"
           "\t}\n}\n"
           "Objects:  {\n"
           "\tGeometry: 1000, \"Geometry::Cube\", \"Mesh\" {\n"
           "\t\tVertices: *72 {\n\t\t\ta: " +
           vertices +
           "\n\t\t}\n"
           "\t\tPolygonVertexIndex: *24 {\n\t\t\ta: " +
           polygons +
           "\n\t\t}\n"
           "\t\tGeometryVersion: 124\n"
           "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
           "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"Direct\"\n"
           "\t\t\tNormals: *72 {\n\t\t\t\ta: " +
           normals +
           "\n\t\t\t}\n\t\t}\n"
           "\t\tLayer: 0 {\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {\n\t\t\t\tType: "
           "\"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n"
           "\t}\n"
           "\tModel: 2000, \"Model::Cube\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t}\n\t}\n"
           "}\n"
           "Connections:  {\n"
           "\tC: \"OO\",1000,2000\n"
           "\tC: \"OO\",2000,0\n"
           "}\n";
}

// --- Images ------------------------------------------------------------------------------------------------------

inline std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0xFFFFFFFFu)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

inline void BigEndian(std::vector<std::byte>& out, std::uint32_t v)
{
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<std::byte>((v >> shift) & 0xFF));
    }
}

inline void PngChunk(std::vector<std::byte>& out, const char (&type)[5], const std::vector<std::byte>& body)
{
    BigEndian(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> crcInput;
    for (int i = 0; i < 4; ++i)
    {
        crcInput.push_back(static_cast<std::uint8_t>(type[i]));
    }
    for (const std::byte b : body)
    {
        crcInput.push_back(std::to_integer<std::uint8_t>(b));
    }
    for (int i = 0; i < 4; ++i)
    {
        out.push_back(static_cast<std::byte>(type[i]));
    }
    out.insert(out.end(), body.begin(), body.end());
    BigEndian(out, ~Crc32(crcInput.data(), crcInput.size()));
}

/// An uncompressed (stored-deflate) 8-bit RGBA PNG. `rgba` has width * height * 4 bytes.
inline std::vector<std::byte> MakePng(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba)
{
    std::vector<std::byte> out = {std::byte{0x89}, std::byte{'P'},  std::byte{'N'},  std::byte{'G'},
                                  std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    std::vector<std::byte> ihdr;
    BigEndian(ihdr, width);
    BigEndian(ihdr, height);
    for (const std::uint8_t b : std::array<std::uint8_t, 5>{8, 6, 0, 0, 0})
    {
        ihdr.push_back(static_cast<std::byte>(b));
    }
    PngChunk(out, "IHDR", ihdr);
    std::vector<std::uint8_t> raw;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(y * width * 4),
                   rgba.begin() + static_cast<std::ptrdiff_t>((y + 1) * width * 4));
    }
    std::vector<std::byte> z = {std::byte{0x78}, std::byte{0x01}};
    std::size_t position = 0;
    do
    {
        const std::size_t length = std::min<std::size_t>(65535, raw.size() - position);
        const bool last = position + length == raw.size();
        z.push_back(static_cast<std::byte>(last ? 1 : 0));
        z.push_back(static_cast<std::byte>(length & 0xFF));
        z.push_back(static_cast<std::byte>(length >> 8));
        z.push_back(static_cast<std::byte>(~length & 0xFF));
        z.push_back(static_cast<std::byte>((~length >> 8) & 0xFF));
        for (std::size_t i = 0; i < length; ++i)
        {
            z.push_back(static_cast<std::byte>(raw[position + i]));
        }
        position += length;
    } while (position < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    BigEndian(z, (b << 16) | a);
    PngChunk(out, "IDAT", z);
    PngChunk(out, "IEND", {});
    return out;
}

/// An uncompressed 24-bit TGA (bottom-left origin). `bgr` is width * height * 3 bytes, bottom row first.
inline std::vector<std::byte> MakeTga(std::uint16_t width, std::uint16_t height, const std::vector<std::uint8_t>& bgr)
{
    std::vector<std::byte> out(18, std::byte{0});
    out[2] = std::byte{2};
    out[12] = static_cast<std::byte>(width & 0xFF);
    out[13] = static_cast<std::byte>(width >> 8);
    out[14] = static_cast<std::byte>(height & 0xFF);
    out[15] = static_cast<std::byte>(height >> 8);
    out[16] = std::byte{24};
    for (const std::uint8_t b : bgr)
    {
        out.push_back(static_cast<std::byte>(b));
    }
    return out;
}

/// An uncompressed Radiance HDR of one repeated pixel (mantissas 128, 64, 32 with exponent 129 = 1.0, 0.5, 0.25).
inline std::vector<std::byte> MakeHdr(std::uint32_t width, std::uint32_t height)
{
    std::vector<std::byte> out = Bytes("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y " + std::to_string(height) + " +X " +
                                       std::to_string(width) + "\n");
    for (std::uint32_t i = 0; i < width * height; ++i)
    {
        for (const std::uint8_t b : std::array<std::uint8_t, 4>{128, 64, 32, 129})
        {
            out.push_back(static_cast<std::byte>(b));
        }
    }
    return out;
}

} // namespace interi::testing_fixtures
