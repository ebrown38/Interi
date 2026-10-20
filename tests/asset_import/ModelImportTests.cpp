// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelImport.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>

#include "ImportFixtures.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

using namespace interi;
using namespace interi::asset_import;
using namespace interi::testing_fixtures;

namespace
{

ModelImportSettings Defaults()
{
    return {};
}

Result<ImportedModel> ImportGltfBytes(const GltfFile& file, const ModelImportSettings& settings = Defaults())
{
    MemoryFileSystem files;
    if (!file.bin.empty())
    {
        EXPECT_TRUE(files.WriteFile("models/cube.bin", file.bin).has_value());
    }
    return ImportGltfData(file.main, files, "models", settings);
}

Result<ImportedModel> ImportObjText(const std::string& text, const ModelImportSettings& settings = Defaults(),
                                    MemoryFileSystem* files = nullptr)
{
    MemoryFileSystem local;
    return ImportObjData(Bytes(text), files != nullptr ? *files : local, "models", settings);
}

/// Each triangle as its three corner positions with the corner normals, rotated to start at the smallest corner: an
/// order-independent signature of the geometry (triangle order and corner rotation may change when optimizing).
std::multiset<std::string> Signatures(const scene::TriangleMesh& mesh)
{
    std::multiset<std::string> result;
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        std::array<std::string, 3> corners;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const Vec3& p = mesh.positions[mesh.indices[3 * t + k]];
            const Vec3& n = mesh.normals[3 * t + k];
            corners[k] = std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z) + "|" +
                         std::to_string(n.x) + "," + std::to_string(n.y) + "," + std::to_string(n.z);
        }
        std::ranges::rotate(corners, std::ranges::min_element(corners));
        result.insert(corners[0] + ";" + corners[1] + ";" + corners[2]);
    }
    return result;
}

void ExpectUnitCube(const ImportedMesh& imported, float size = 1.0f)
{
    const scene::TriangleMesh& mesh = imported.mesh;
    EXPECT_TRUE(scene::ValidateMesh(mesh).has_value());
    EXPECT_EQ(mesh.positions.size(), 8u);
    EXPECT_EQ(mesh.TriangleCount(), 12u);
    EXPECT_TRUE(scene::IsClosedManifold(mesh));
    EXPECT_NEAR(scene::MeshVolume(mesh), std::pow(static_cast<double>(size), 3.0), 1e-4);
}

constexpr float kEps = 1e-5f;

} // namespace

// --- glTF ---------------------------------------------------------------------------------------------------------

TEST(GltfImport, Cube_WeldsToAClosedManifoldWithUnitVolume)
{
    auto model = ImportGltfBytes(MakeCubeGltf());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->meshes.size(), 1u);
    EXPECT_EQ(model->meshes[0].name, "Cube");
    ExpectUnitCube(model->meshes[0]);
    EXPECT_EQ(model->meshes[0].mesh.uvs.size(), model->meshes[0].mesh.indices.size());
    EXPECT_EQ(model->meshes[0].mesh.tangents.size(), model->meshes[0].mesh.indices.size());
    EXPECT_EQ(model->meshes[0].slotMaterials, (std::vector<std::int32_t>{-1}));
}

TEST(GltfImport, FileNormals_AreKeptPerFace)
{
    auto model = ImportGltfBytes(MakeCubeGltf());
    ASSERT_TRUE(model.has_value());
    const scene::TriangleMesh& mesh = model->meshes[0].mesh;
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const Vec3& a = mesh.positions[mesh.indices[3 * t]];
        const Vec3 face =
            Normalize(Cross(mesh.positions[mesh.indices[3 * t + 1]] - a, mesh.positions[mesh.indices[3 * t + 2]] - a));
        for (std::size_t k = 0; k < 3; ++k)
        {
            EXPECT_NEAR(Dot(face, mesh.normals[3 * t + k]), 1.0f, kEps);
        }
    }
}

TEST(GltfImport, Scale_MultipliesSize)
{
    ModelImportSettings settings;
    settings.scale = 3.0f;
    auto model = ImportGltfBytes(MakeCubeGltf(), settings);
    ASSERT_TRUE(model.has_value());
    ExpectUnitCube(model->meshes[0], 3.0f);
}

TEST(GltfImport, NodeTransform_IsBakedIn)
{
    GltfOptions options;
    options.nodeExtra = {{"translation", {10.0, 0.0, 0.0}}, {"scale", {2.0, 2.0, 2.0}}};
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value());
    const scene::MeshBounds bounds = scene::BoundsOf(model->meshes[0].mesh);
    EXPECT_NEAR(bounds.min.x, 9.0f, kEps);
    EXPECT_NEAR(bounds.max.x, 11.0f, kEps);
    EXPECT_NEAR(bounds.max.y, 1.0f, kEps);
    EXPECT_NEAR(scene::MeshVolume(model->meshes[0].mesh), 8.0, 1e-4);
}

TEST(GltfImport, NegativeNodeScale_KeepsFacesOutward)
{
    GltfOptions options;
    options.nodeExtra = {{"scale", {-1.0, 1.0, 1.0}}};
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value());
    ExpectUnitCube(model->meshes[0]);
    EXPECT_GT(scene::MeshVolume(model->meshes[0].mesh), 0.0);
}

TEST(GltfImport, ZUpSettings_RotateToYUp)
{
    GltfOptions options;
    options.nodeExtra = {{"translation", {0.0, 0.0, 5.0}}}; // 5 along the source's up axis (Z)
    ModelImportSettings settings;
    settings.up = UpAxis::Z;
    auto model = ImportGltfBytes(MakeCubeGltf(options), settings);
    ASSERT_TRUE(model.has_value());
    const scene::MeshBounds bounds = scene::BoundsOf(model->meshes[0].mesh);
    EXPECT_NEAR(bounds.max.y, 5.5f, kEps); // up is +Y now
    EXPECT_NEAR(bounds.min.y, 4.5f, kEps);
    EXPECT_NEAR(bounds.min.z, -0.5f, kEps);
    EXPECT_NEAR(scene::MeshVolume(model->meshes[0].mesh), 1.0, 1e-4);
}

TEST(GltfImport, LeftHandedSettings_MirrorAndKeepWinding)
{
    GltfOptions options;
    options.nodeExtra = {{"translation", {0.0, 0.0, 5.0}}};
    ModelImportSettings settings;
    settings.handedness = Handedness::Left;
    auto model = ImportGltfBytes(MakeCubeGltf(options), settings);
    ASSERT_TRUE(model.has_value());
    const scene::MeshBounds bounds = scene::BoundsOf(model->meshes[0].mesh);
    EXPECT_NEAR(bounds.max.z, -4.5f, kEps); // z is mirrored
    EXPECT_GT(scene::MeshVolume(model->meshes[0].mesh), 0.9);
    ExpectUnitCube(model->meshes[0]);
}

TEST(GltfImport, ComputedNormals_FlatAndSmooth)
{
    ModelImportSettings flat;
    flat.normals = NormalMode::Flat;
    auto flatModel = ImportGltfBytes(MakeCubeGltf(), flat);
    ASSERT_TRUE(flatModel.has_value());
    for (const Vec3& n : flatModel->meshes[0].mesh.normals)
    {
        EXPECT_NEAR(Length(n), 1.0f, kEps);
        EXPECT_NEAR(std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)}), 1.0f, kEps); // axis aligned
    }
    ModelImportSettings smooth;
    smooth.normals = NormalMode::Smooth;
    smooth.smoothingAngleDegrees = 180.0f; // everything shares
    auto smoothModel = ImportGltfBytes(MakeCubeGltf(), smooth);
    ASSERT_TRUE(smoothModel.has_value());
    for (const Vec3& n : smoothModel->meshes[0].mesh.normals)
    {
        EXPECT_NEAR(std::abs(n.x), 1.0f / std::sqrt(3.0f), 1e-4f); // the cube's corner diagonals
    }
    ModelImportSettings sharp;
    sharp.normals = NormalMode::Smooth;
    sharp.smoothingAngleDegrees = 60.0f; // 90 degree edges stay sharp
    auto sharpModel = ImportGltfBytes(MakeCubeGltf(), sharp);
    ASSERT_TRUE(sharpModel.has_value());
    for (const Vec3& n : sharpModel->meshes[0].mesh.normals)
    {
        EXPECT_NEAR(std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)}), 1.0f, kEps);
    }
}

TEST(GltfImport, Tangents_AreGeneratedFromUvs)
{
    auto model = ImportGltfBytes(MakeCubeGltf());
    ASSERT_TRUE(model.has_value());
    const scene::TriangleMesh& mesh = model->meshes[0].mesh;
    for (std::size_t c = 0; c < mesh.indices.size(); ++c)
    {
        const Vec3 tangent = mesh.tangents[c].XYZ();
        EXPECT_NEAR(Length(tangent), 1.0f, 1e-4f);
        EXPECT_NEAR(Dot(tangent, mesh.normals[c]), 0.0f, 1e-4f);
        EXPECT_TRUE(mesh.tangents[c].w == 1.0f || mesh.tangents[c].w == -1.0f);
    }
}

TEST(GltfImport, TangentsOff_LeavesThemEmpty)
{
    ModelImportSettings settings;
    settings.generateTangents = false;
    auto model = ImportGltfBytes(MakeCubeGltf(), settings);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->meshes[0].mesh.tangents.empty());
}

TEST(GltfImport, Optimize_KeepsTheGeometry)
{
    ModelImportSettings off;
    off.optimize = false;
    auto plain = ImportGltfBytes(MakeCubeGltf(), off);
    auto optimized = ImportGltfBytes(MakeCubeGltf(), Defaults());
    ASSERT_TRUE(plain.has_value());
    ASSERT_TRUE(optimized.has_value());
    EXPECT_EQ(Signatures(plain->meshes[0].mesh), Signatures(optimized->meshes[0].mesh));
    EXPECT_TRUE(scene::ValidateMesh(optimized->meshes[0].mesh).has_value());
}

TEST(GltfImport, Materials_AreMapped)
{
    GltfOptions options;
    options.materials = nlohmann::json::array({{{"name", "Brick"},
                                                {"pbrMetallicRoughness",
                                                 {{"baseColorFactor", {0.5, 0.25, 0.125, 0.5}},
                                                  {"metallicFactor", 0.25},
                                                  {"roughnessFactor", 0.75},
                                                  {"baseColorTexture", {{"index", 0}, {"texCoord", 1}}}}},
                                                {"normalTexture", {{"index", 0}, {"scale", 0.5}}},
                                                {"emissiveFactor", {1.0, 0.5, 0.0}},
                                                {"alphaMode", "MASK"},
                                                {"alphaCutoff", 0.4},
                                                {"doubleSided", true}}});
    options.extraRoot = {{"images", nlohmann::json::array({nlohmann::json{{"uri", "textures/brick%20wall.png"}}})},
                         {"textures", nlohmann::json::array({nlohmann::json{{"source", 0}}})}};
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->materials.size(), 1u);
    const assets::PbrMaterial& m = model->materials[0];
    EXPECT_EQ(m.name, "Brick");
    EXPECT_EQ(m.baseColor, Vec4(0.5f, 0.25f, 0.125f, 0.5f));
    EXPECT_FLOAT_EQ(m.metallic, 0.25f);
    EXPECT_FLOAT_EQ(m.roughness, 0.75f);
    EXPECT_EQ(m.emissive, Vec3(1.0f, 0.5f, 0.0f));
    EXPECT_EQ(m.alphaMode, assets::AlphaMode::Mask);
    EXPECT_FLOAT_EQ(m.alphaCutoff, 0.4f);
    EXPECT_TRUE(m.doubleSided);
    EXPECT_EQ(m.baseColorTexture.path, "models/textures/brick wall.png");
    EXPECT_EQ(m.baseColorTexture.uvSet, 1u);
    EXPECT_EQ(m.normalTexture.path, "models/textures/brick wall.png");
    EXPECT_FLOAT_EQ(m.normalScale, 0.5f);
    EXPECT_TRUE(assets::ValidateMaterial(m).has_value());
    EXPECT_EQ(model->meshes[0].slotMaterials, (std::vector<std::int32_t>{0}));
}

TEST(GltfImport, MaterialsOff_SkipsThem)
{
    GltfOptions options;
    options.materials = nlohmann::json::array({{{"name", "A"}}});
    ModelImportSettings settings;
    settings.importMaterials = false;
    auto model = ImportGltfBytes(MakeCubeGltf(options), settings);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->materials.empty());
    EXPECT_EQ(model->meshes[0].slotMaterials, (std::vector<std::int32_t>{-1}));
}

TEST(GltfImport, OutOfRangeMaterialFactors_AreClamped)
{
    GltfOptions options;
    options.materials =
        nlohmann::json::array({{{"pbrMetallicRoughness", {{"metallicFactor", 5.0}, {"roughnessFactor", -2.0}}}}});
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(assets::ValidateMaterial(model->materials[0]).has_value());
}

TEST(GltfImport, Glb_WithEmbeddedImage)
{
    GltfOptions options;
    options.glb = true;
    options.materials = nlohmann::json::array({{{"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
    // The image lives in the BIN chunk after the geometry: add a second buffer view for it.
    // (MakeCubeGltf appends views itself, so declare the image through a data URI-less bufferView at the end.)
    options.extraRoot = {
        {"images", nlohmann::json::array({nlohmann::json{{"mimeType", "image/png"}, {"bufferView", 4}}})},
        {"textures", nlohmann::json::array({nlohmann::json{{"source", 0}}})}};
    GltfFile file = MakeCubeGltf(options);
    // Re-create with a bufferView 4 that points at the png: patch by building the document directly.
    auto png = MakePng(1, 1, {255, 0, 0, 255});
    // Build the GLB by hand through the same helper: append the PNG to the BIN chunk and a bufferView for it.
    // Layout: JSON chunk then BIN chunk; find the BIN chunk and patch the JSON.
    std::uint32_t jsonLength = 0;
    std::memcpy(&jsonLength, file.main.data() + 12, 4);
    std::string json(reinterpret_cast<const char*>(file.main.data() + 20), jsonLength);
    nlohmann::json doc = nlohmann::json::parse(json);
    std::vector<std::byte> bin(file.main.begin() + 20 + jsonLength + 8, file.main.end());
    while (bin.size() % 4 != 0)
    {
        bin.push_back(std::byte{0});
    }
    doc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", bin.size()}, {"byteLength", png.size()}});
    bin.insert(bin.end(), png.begin(), png.end());
    while (bin.size() % 4 != 0)
    {
        bin.push_back(std::byte{0});
    }
    doc["buffers"][0]["byteLength"] = bin.size();
    std::string text = doc.dump();
    while (text.size() % 4 != 0)
    {
        text.push_back(' ');
    }
    std::vector<std::byte> glb;
    Append(glb, std::uint32_t{0x46546C67});
    Append(glb, std::uint32_t{2});
    Append(glb, static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
    Append(glb, static_cast<std::uint32_t>(text.size()));
    Append(glb, std::uint32_t{0x4E4F534A});
    const auto textBytes = Bytes(text);
    glb.insert(glb.end(), textBytes.begin(), textBytes.end());
    Append(glb, static_cast<std::uint32_t>(bin.size()));
    Append(glb, std::uint32_t{0x004E4942});
    glb.insert(glb.end(), bin.begin(), bin.end());
    MemoryFileSystem files;
    auto model = ImportGltfData(glb, files, "", Defaults());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ExpectUnitCube(model->meshes[0]);
    ASSERT_EQ(model->images.size(), 1u);
    EXPECT_EQ(model->images[0].bytes, png);
    EXPECT_EQ(model->materials[0].baseColorTexture.path, "embedded:0");
}

TEST(GltfImport, ExternalBuffer_IsReadThroughTheFileSystem)
{
    GltfOptions options;
    options.externalBuffer = true;
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ExpectUnitCube(model->meshes[0]);
}

TEST(GltfImport, MissingExternalBuffer_IsNotFound)
{
    GltfOptions options;
    options.externalBuffer = true;
    GltfFile file = MakeCubeGltf(options);
    MemoryFileSystem files;
    auto model = ImportGltfData(file.main, files, "models", Defaults());
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::NotFound);
}

TEST(GltfImport, BufferOutsideTheFileSystem_IsRefused)
{
    GltfOptions options;
    options.externalBuffer = true;
    options.bufferUri = "../secret.bin";
    GltfFile file = MakeCubeGltf(options);
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("secret.bin", file.bin).has_value());
    auto model = ImportGltfData(file.main, files, "models", Defaults());
    ASSERT_FALSE(model.has_value());
}

TEST(GltfImport, ImageOutsideTheFileSystem_IsAWarningNotARead)
{
    GltfOptions options;
    options.materials = nlohmann::json::array({{{"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
    options.extraRoot = {{"images", nlohmann::json::array({nlohmann::json{{"uri", "../../etc/passwd"}}})},
                         {"textures", nlohmann::json::array({nlohmann::json{{"source", 0}}})}};
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->materials[0].baseColorTexture.path.empty());
    EXPECT_FALSE(model->warnings.empty());
}

TEST(GltfImport, IndexOutOfRange_IsCorrupt)
{
    GltfOptions options;
    options.indexOverride = 500;
    auto model = ImportGltfBytes(MakeCubeGltf(options));
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::Corrupt);
}

namespace
{

/// One triangle whose POSITION accessor is sparse: a zeroed base of three `type` elements (a view with `byteStride`
/// when non-zero) and sparse values moving vertex 1 to (1, 0, 0) and vertex 2 to (0, 1, 0).
GltfFile MakeSparseTriangleGltf(const char* type = "VEC3", std::size_t components = 3, std::size_t byteStride = 0)
{
    GltfBuffer buffer;
    const std::size_t stride = byteStride != 0 ? byteStride : components * sizeof(float);
    std::vector<float> values(2 * components, 0.0f);
    values[0] = 1.0f;              // vertex 1: x
    values[components + 1] = 1.0f; // vertex 2: y
    const int indices = buffer.Add(Pack(std::vector<std::uint16_t>{0, 1, 2}), 5123, 3, "SCALAR", 34963);
    const int positions = buffer.Add(std::vector<std::byte>(3 * stride), 5126, 3, type, 34962);
    const std::size_t positionView = buffer.bufferViews.size() - 1;
    if (byteStride != 0)
    {
        buffer.bufferViews[positionView]["byteStride"] = byteStride;
    }
    // The sparse indices and values are views without accessors of their own.
    const auto addView = [&](const std::vector<std::byte>& data)
    {
        buffer.bufferViews.push_back({{"buffer", 0}, {"byteOffset", buffer.bytes.size()}, {"byteLength", data.size()}});
        buffer.bytes.insert(buffer.bytes.end(), data.begin(), data.end());
        return buffer.bufferViews.size() - 1;
    };
    const std::size_t sparseIndices = addView(Pack(std::vector<std::uint16_t>{1, 2, 0, 0})); // padded to 4 bytes
    const std::size_t sparseValues = addView(Pack(values));
    buffer.accessors[static_cast<std::size_t>(positions)]["sparse"] = {
        {"count", 2},
        {"indices", {{"bufferView", sparseIndices}, {"componentType", 5123}}},
        {"values", {{"bufferView", sparseValues}}}};
    nlohmann::json doc = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0}}}}},
        {"nodes", {{{"mesh", 0}}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", positions}}}, {"indices", indices}}}}}}},
        {"accessors", buffer.accessors},
        {"bufferViews", buffer.bufferViews},
        {"buffers",
         {{{"byteLength", buffer.bytes.size()},
           {"uri", "data:application/octet-stream;base64," + Base64(buffer.bytes)}}}}};
    return {.main = Bytes(doc.dump()), .bin = {}};
}

} // namespace

TEST(GltfImport, SparsePositions_AreApplied)
{
    auto model = ImportGltfBytes(MakeSparseTriangleGltf());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->meshes.size(), 1u);
    const auto bounds = scene::BoundsOf(model->meshes[0].mesh);
    EXPECT_EQ(bounds.max, Vec3(1, 1, 0));
}

// cgltf 1.15's sparse pass writes past an output sized for fewer components (CVE-2026-112018) and steps through the
// sparse values by the base view's stride (CVE-2026-112019); the importer refuses both shapes (ASan catches a miss).
TEST(GltfImport, SparseAccessorOfAnotherType_IsCorrupt)
{
    auto model = ImportGltfBytes(MakeSparseTriangleGltf("VEC4", 4));
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::Corrupt);
}

TEST(GltfImport, SparseAccessorWithAByteStride_IsCorrupt)
{
    auto model = ImportGltfBytes(MakeSparseTriangleGltf("VEC3", 3, 16));
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::Corrupt);
}

TEST(GltfImport, NotGltf_IsCorrupt)
{
    MemoryFileSystem files;
    EXPECT_FALSE(ImportGltfData(Bytes("{ not json"), files, "", Defaults()).has_value());
    EXPECT_FALSE(ImportGltfData(Bytes(""), files, "", Defaults()).has_value());
    EXPECT_FALSE(ImportGltfData(Bytes("{}"), files, "", Defaults()).has_value());
}

TEST(GltfImport, BadSettings_AreRefused)
{
    ModelImportSettings settings;
    settings.scale = 0.0f;
    auto model = ImportGltfBytes(MakeCubeGltf(), settings);
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::InvalidArgument);
    settings = {};
    settings.smoothingAngleDegrees = 400.0f;
    EXPECT_FALSE(ImportGltfBytes(MakeCubeGltf(), settings).has_value());
}

// --- Skins and animation --------------------------------------------------------------------------------------------

namespace
{

/// A one-triangle skinned mesh: joint 0 ("root") at the origin, joint 1 ("arm") a child at (0, 2, 0); one animation
/// moves the arm along Y and rotates the root a quarter turn about Y.
GltfFile MakeSkinnedGltf()
{
    GltfBuffer buffer;
    const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const int pos = buffer.Add(PackVec3(positions), 5126, 3, "VEC3", 34962);
    std::vector<std::byte> jointBytes;
    for (int v = 0; v < 3; ++v)
    {
        for (const std::uint8_t j :
             {static_cast<std::uint8_t>(v == 2 ? 1 : 0), std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{0}})
        {
            jointBytes.push_back(static_cast<std::byte>(j));
        }
    }
    const int joints = buffer.Add(jointBytes, 5121, 3, "VEC4", 34962);
    std::vector<float> weightValues;
    for (int v = 0; v < 3; ++v)
    {
        for (const float w : {v == 2 ? 1.0f : 0.5f, v == 2 ? 0.0f : 0.5f, 0.0f, 0.0f})
        {
            weightValues.push_back(w);
        }
    }
    const int weights = buffer.Add(Pack(weightValues), 5126, 3, "VEC4", 34962);
    const int indices = buffer.Add(Pack(std::vector<std::uint16_t>{0, 1, 2}), 5123, 3, "SCALAR", 34963);
    // Inverse bind matrices: identity, and translate(0, -2, 0) (column-major).
    std::vector<float> ibm = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,  0, 1,
                              1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1};
    const int ibmAccessor = buffer.Add(Pack(ibm), 5126, 2, "MAT4");
    const int times =
        buffer.Add(Pack(std::vector<float>{0.0f, 1.0f}), 5126, 2, "SCALAR", 0, {{"min", {0.0}}, {"max", {1.0}}});
    const int armKeys = buffer.Add(PackVec3({{0, 2, 0}, {0, 3, 0}}), 5126, 2, "VEC3");
    std::vector<float> rootKeys = {0, 0, 0, 1, 0, std::sin(0.7853982f), 0, std::cos(0.7853982f)};
    const int rootAccessor = buffer.Add(Pack(rootKeys), 5126, 2, "VEC4");
    nlohmann::json doc = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0, 2}}}}},
        {"nodes",
         {{{"name", "root"}, {"children", {1}}},
          {{"name", "arm"}, {"translation", {0, 2, 0}}},
          {{"name", "body"}, {"mesh", 0}, {"skin", 0}, {"translation", {100, 0, 0}}}}}, // ignored for skinned meshes
        {"skins", {{{"joints", {0, 1}}, {"inverseBindMatrices", ibmAccessor}, {"name", "rig"}}}},
        {"meshes",
         {{{"primitives",
            {{{"attributes", {{"POSITION", pos}, {"JOINTS_0", joints}, {"WEIGHTS_0", weights}}},
              {"indices", indices}}}}}}},
        {"animations",
         {{{"name", "wave"},
           {"samplers",
            {{{"input", times}, {"output", armKeys}, {"interpolation", "LINEAR"}},
             {{"input", times}, {"output", rootAccessor}, {"interpolation", "STEP"}}}},
           {"channels",
            {{{"sampler", 0}, {"target", {{"node", 1}, {"path", "translation"}}}},
             {{"sampler", 1}, {"target", {{"node", 0}, {"path", "rotation"}}}}}}}}},
        {"accessors", buffer.accessors},
        {"bufferViews", buffer.bufferViews},
        {"buffers",
         {{{"byteLength", buffer.bytes.size()},
           {"uri", "data:application/octet-stream;base64," + Base64(buffer.bytes)}}}}};
    return {.main = Bytes(doc.dump()), .bin = {}};
}

} // namespace

TEST(GltfImport, Skin_BecomesASkeletonWithInfluences)
{
    auto model = ImportGltfBytes(MakeSkinnedGltf());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->skeletons.size(), 1u);
    const assets::Skeleton& skeleton = model->skeletons[0];
    EXPECT_EQ(skeleton.name, "rig");
    ASSERT_EQ(skeleton.joints.size(), 2u);
    EXPECT_EQ(skeleton.joints[0].name, "root");
    EXPECT_EQ(skeleton.joints[0].parent, -1);
    EXPECT_EQ(skeleton.joints[1].parent, 0);
    EXPECT_EQ(skeleton.joints[1].bind.translation, Vec3(0, 2, 0));
    EXPECT_EQ(skeleton.joints[1].inverseBind, Mat4::Translation(Vec3(0, -2, 0)));
    ASSERT_EQ(model->meshes.size(), 1u);
    const ImportedMesh& mesh = model->meshes[0];
    EXPECT_EQ(mesh.skeleton, 0);
    ASSERT_EQ(mesh.skin.size(), 3u);
    EXPECT_TRUE(assets::ValidateSkin({.skeleton = "", .influences = mesh.skin}, 2).has_value());
    // The node's own translation (100, 0, 0) must not have been applied.
    EXPECT_LT(scene::BoundsOf(mesh.mesh).max.x, 2.0f);
}

TEST(GltfImport, Animation_BecomesAClipOnTheSkeleton)
{
    auto model = ImportGltfBytes(MakeSkinnedGltf());
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->clips.size(), 1u);
    const assets::AnimationClip& clip = model->clips[0].clip;
    EXPECT_EQ(model->clips[0].skeleton, 0);
    EXPECT_EQ(clip.name, "wave");
    EXPECT_FLOAT_EQ(clip.duration, 1.0f);
    ASSERT_EQ(clip.channels.size(), 2u);
    EXPECT_TRUE(assets::ValidateClip(clip, 2).has_value());
    const auto& translation = clip.channels[0];
    EXPECT_EQ(translation.joint, 1u);
    EXPECT_EQ(translation.path, assets::ChannelPath::Translation);
    EXPECT_EQ(translation.interpolation, assets::Interpolation::Linear);
    EXPECT_EQ(translation.values[1], Vec4(0, 3, 0, 0));
    EXPECT_EQ(clip.channels[1].interpolation, assets::Interpolation::Step);
}

TEST(GltfImport, SkeletonAndClip_FollowTheAxisConversion)
{
    ModelImportSettings settings;
    settings.up = UpAxis::Z;
    settings.scale = 2.0f;
    auto model = ImportGltfBytes(MakeSkinnedGltf(), settings);
    ASSERT_TRUE(model.has_value());
    // Source (0, 2, 0) -> scaled (0, 4, 0) -> Z-up to Y-up (x, z, -y): (0, 0, -4).
    const Vec3 arm = model->skeletons[0].joints[1].bind.translation;
    EXPECT_NEAR(arm.x, 0.0f, kEps);
    EXPECT_NEAR(arm.y, 0.0f, kEps);
    EXPECT_NEAR(arm.z, -4.0f, kEps);
    const Vec4 key = model->clips[0].clip.channels[0].values[1]; // source (0, 3, 0)
    EXPECT_NEAR(key.z, -6.0f, kEps);
    // The skin matrix of the arm still maps its bind pose to the identity: world(bind) * inverseBind = I.
    const auto& joints = model->skeletons[0].joints;
    const Mat4 root = Mat4::FromTRS(joints[0].bind.translation, joints[0].bind.rotation, joints[0].bind.scale);
    const Mat4 arm4 = root * Mat4::FromTRS(joints[1].bind.translation, joints[1].bind.rotation, joints[1].bind.scale);
    const Mat4 skin = arm4 * joints[1].inverseBind;
    for (int c = 0; c < 4; ++c)
    {
        const Vec4 expected = Mat4::Identity().columns[c];
        EXPECT_NEAR(skin.columns[c].x, expected.x, 1e-4f);
        EXPECT_NEAR(skin.columns[c].y, expected.y, 1e-4f);
        EXPECT_NEAR(skin.columns[c].z, expected.z, 1e-4f);
        EXPECT_NEAR(skin.columns[c].w, expected.w, 1e-4f);
    }
}

TEST(GltfImport, SkeletonsOff_ImportsAStaticMesh)
{
    ModelImportSettings settings;
    settings.importSkeletons = false;
    auto model = ImportGltfBytes(MakeSkinnedGltf(), settings);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->skeletons.empty());
    EXPECT_TRUE(model->clips.empty());
    EXPECT_EQ(model->meshes[0].skeleton, -1);
    EXPECT_GT(scene::BoundsOf(model->meshes[0].mesh).min.x, 99.0f); // the node transform applies now
}

// --- OBJ -------------------------------------------------------------------------------------------------------------

TEST(ObjImport, QuadCube_IsTriangulatedAndWelded)
{
    auto model = ImportObjText(MakeCubeObj());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->meshes.size(), 1u);
    ExpectUnitCube(model->meshes[0]);
}

TEST(ObjImport, Uvs_FlipToTopLeftOrigin)
{
    auto model = ImportObjText("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n");
    ASSERT_TRUE(model.has_value()) << model.error().message;
    const scene::TriangleMesh& mesh = model->meshes[0].mesh;
    ASSERT_EQ(mesh.uvs.size(), 3u);
    for (std::size_t c = 0; c < 3; ++c)
    {
        const Vec3& p = mesh.positions[mesh.indices[c]];
        // OBJ (u, v) = (x, y); ours is (x, 1 - y).
        EXPECT_NEAR(mesh.uvs[c].u, p.x, kEps);
        EXPECT_NEAR(mesh.uvs[c].v, 1.0f - p.y, kEps);
    }
}

TEST(ObjImport, TangentSign_FollowsTheUvHandedness)
{
    // A quad in the XY plane; u grows with x and (top-left origin) v shrinks as y grows: the image is upright.
    auto upright = ImportObjText("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
                                 "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
    ASSERT_TRUE(upright.has_value()) << upright.error().message;
    for (const Vec4& t : upright->meshes[0].mesh.tangents)
    {
        EXPECT_NEAR(t.x, 1.0f, kEps);
        EXPECT_FLOAT_EQ(t.w, 1.0f);
    }
    // Mirror u: the tangent flips with the mapping and the bitangent sign follows.
    auto mirrored = ImportObjText("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 1 0\nvt 0 0\nvt 0 1\nvt 1 1\nvn 0 0 1\n"
                                  "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
    ASSERT_TRUE(mirrored.has_value());
    for (const Vec4& t : mirrored->meshes[0].mesh.tangents)
    {
        EXPECT_NEAR(t.x, -1.0f, kEps);
        EXPECT_FLOAT_EQ(t.w, -1.0f);
    }
}

TEST(ObjImport, NoNormals_AreGeneratedSmooth)
{
    auto model = ImportObjText(MakeCubeObj(false, false));
    ASSERT_TRUE(model.has_value());
    const scene::TriangleMesh& mesh = model->meshes[0].mesh;
    ASSERT_EQ(mesh.normals.size(), mesh.indices.size());
    for (const Vec3& n : mesh.normals)
    {
        EXPECT_NEAR(Length(n), 1.0f, kEps);
        EXPECT_NEAR(std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)}), 1.0f, kEps); // 90 degrees > 60: sharp
    }
    EXPECT_TRUE(mesh.uvs.empty());
    EXPECT_TRUE(mesh.tangents.empty());
}

TEST(ObjImport, ZUpAndScale_Apply)
{
    ModelImportSettings settings;
    settings.up = UpAxis::Z;
    settings.scale = 0.5f;
    auto model = ImportObjText("v 0 0 4\nv 2 0 4\nv 0 2 4\nf 1 2 3\n", settings);
    ASSERT_TRUE(model.has_value());
    const scene::MeshBounds bounds = scene::BoundsOf(model->meshes[0].mesh);
    EXPECT_NEAR(bounds.min.y, 2.0f, kEps); // z = 4 * 0.5 is the height
    EXPECT_NEAR(bounds.max.y, 2.0f, kEps);
    EXPECT_NEAR(bounds.min.z, -1.0f, kEps); // y = 2 * 0.5 maps to -z
}

TEST(ObjImport, MaterialsAndSlots_FromMtl)
{
    MemoryFileSystem files;
    ASSERT_TRUE(files
                    .WriteFile("models/m.mtl", Bytes("newmtl Red\nKd 1 0 0\nmap_Kd tex/red.png\nNs 10\n"
                                                     "newmtl Glass\nKd 0 0 1\nd 0.5\nKe 0.5 0 0\n"))
                    .has_value());
    auto model =
        ImportObjText("mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\nusemtl Red\nf 1 2 3\nusemtl Glass\nf 2 4 3\n",
                      Defaults(), &files);
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->materials.size(), 2u);
    EXPECT_EQ(model->materials[0].name, "Red");
    EXPECT_EQ(model->materials[0].baseColor, Vec4(1, 0, 0, 1));
    EXPECT_EQ(model->materials[0].baseColorTexture.path, "models/tex/red.png");
    EXPECT_EQ(model->materials[1].alphaMode, assets::AlphaMode::Blend);
    EXPECT_FLOAT_EQ(model->materials[1].baseColor.w, 0.5f);
    EXPECT_EQ(model->materials[1].emissive, Vec3(0.5f, 0, 0));
    EXPECT_TRUE(assets::ValidateMaterial(model->materials[0]).has_value());
    ASSERT_EQ(model->meshes.size(), 1u);
    EXPECT_EQ(model->meshes[0].slotMaterials, (std::vector<std::int32_t>{0, 1}));
    EXPECT_EQ(model->meshes[0].mesh.SlotCount(), 2u);
}

TEST(ObjImport, MissingMtl_IsNotFatal)
{
    auto model = ImportObjText("mtllib nope.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    ASSERT_TRUE(model.has_value()) << model.error().message;
    EXPECT_EQ(model->meshes.size(), 1u);
}

TEST(ObjImport, MtlOutsideTheFileSystem_IsNotRead)
{
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("secret.mtl", Bytes("newmtl Leak\nKd 1 1 1\n")).has_value());
    auto model =
        ImportObjText("mtllib ../secret.mtl\nusemtl Leak\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", Defaults(), &files);
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->materials.empty());
}

TEST(ObjImport, DegenerateTriangles_AreDroppedWithAWarning)
{
    auto model = ImportObjText("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 0\nf 1 2 3\nf 1 2 4\n");
    ASSERT_TRUE(model.has_value()) << model.error().message;
    EXPECT_EQ(model->meshes[0].mesh.TriangleCount(), 1u);
    EXPECT_FALSE(model->warnings.empty());
}

TEST(ObjImport, IndexOutOfRange_IsAnError)
{
    EXPECT_FALSE(ImportObjText("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n").has_value());
}

TEST(ObjImport, NoGeometry_GivesAnEmptyModel)
{
    auto model = ImportObjText("# nothing here\n");
    ASSERT_TRUE(model.has_value());
    EXPECT_TRUE(model->meshes.empty());
}

TEST(ObjImport, MultipleObjects_BecomeMeshes)
{
    auto model = ImportObjText("o A\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no B\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n");
    ASSERT_TRUE(model.has_value());
    ASSERT_EQ(model->meshes.size(), 2u);
    EXPECT_EQ(model->meshes[0].name, "A");
    EXPECT_EQ(model->meshes[1].name, "B");
}

// --- FBX -------------------------------------------------------------------------------------------------------------

TEST(FbxImport, AsciiCube_ConvertsUnitsToMetres)
{
    auto model = ImportFbxData(Bytes(MakeCubeFbx()), "models", Defaults());
    ASSERT_TRUE(model.has_value()) << model.error().message;
    ASSERT_EQ(model->meshes.size(), 1u);
    ExpectUnitCube(model->meshes[0]);
}

TEST(FbxImport, Scale_MultipliesOnTopOfMetres)
{
    ModelImportSettings settings;
    settings.scale = 2.0f;
    auto model = ImportFbxData(Bytes(MakeCubeFbx()), "", settings);
    ASSERT_TRUE(model.has_value());
    ExpectUnitCube(model->meshes[0], 2.0f);
}

TEST(FbxImport, Garbage_IsCorrupt)
{
    auto model = ImportFbxData(Bytes("this is not an fbx file"), "", Defaults());
    ASSERT_FALSE(model.has_value());
    EXPECT_EQ(model.error().code, ErrorCode::Corrupt);
    EXPECT_FALSE(ImportFbxData({}, "", Defaults()).has_value());
}

// --- Dispatch
// ---------------------------------------------------------------------------------------------------------

TEST(ModelImport, ByExtension_ReadsThroughTheFileSystem)
{
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("props/crate.obj", Bytes(MakeCubeObj())).has_value());
    GltfFile gltf = MakeCubeGltf();
    ASSERT_TRUE(files.WriteFile("props/Crate2.GLTF", gltf.main).has_value());
    ASSERT_TRUE(files.WriteFile("props/crate3.fbx", Bytes(MakeCubeFbx())).has_value());
    for (const char* path : {"props/crate.obj", "props/Crate2.GLTF", "props/crate3.fbx"})
    {
        auto model = ImportModel(files, path, Defaults());
        ASSERT_TRUE(model.has_value()) << path << ": " << model.error().message;
        EXPECT_EQ(model->meshes.size(), 1u) << path;
    }
    EXPECT_EQ(ImportModel(files, "props/crate.obj", Defaults())->name, "crate");
}

TEST(ModelImport, UnknownExtensionAndMissingFile_AreErrors)
{
    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("a.txt", Bytes("x")).has_value());
    EXPECT_EQ(ImportModel(files, "a.txt", Defaults()).error().code, ErrorCode::Unsupported);
    EXPECT_EQ(ImportModel(files, "missing.obj", Defaults()).error().code, ErrorCode::NotFound);
    EXPECT_FALSE(ImportModel(files, "../escape.obj", Defaults()).has_value());
}

TEST(ModelImport, IsModelPath_RecognisesTheFormats)
{
    EXPECT_TRUE(IsModelPath("a/b.GLB"));
    EXPECT_TRUE(IsModelPath("x.fbx"));
    EXPECT_FALSE(IsModelPath("x.png"));
    EXPECT_FALSE(IsModelPath("obj"));
}
