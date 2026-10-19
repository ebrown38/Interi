// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Primitives.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <utility>

using namespace interi;
using namespace interi::scene;

namespace
{

constexpr double kPi = std::numbers::pi;

PrimitiveParams Shape(PrimitiveShape shape)
{
    PrimitiveParams params;
    params.shape = shape;
    return params;
}

TriangleMesh Make(const PrimitiveParams& params)
{
    auto mesh = GeneratePrimitive(params);
    EXPECT_TRUE(mesh.has_value()) << ToString(params.shape) << ": " << mesh.error().message;
    return mesh.value_or(TriangleMesh{});
}

/// V - E + F over the welded vertices the triangles use.
long EulerCharacteristic(const TriangleMesh& mesh)
{
    std::set<std::uint32_t> vertices(mesh.indices.begin(), mesh.indices.end());
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        for (int e = 0; e < 3; ++e)
        {
            const std::uint32_t a = mesh.indices[3 * t + static_cast<std::size_t>(e)];
            const std::uint32_t b = mesh.indices[3 * t + static_cast<std::size_t>((e + 1) % 3)];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    }
    return static_cast<long>(vertices.size()) - static_cast<long>(edges.size()) +
           static_cast<long>(mesh.TriangleCount());
}

/// Every corner normal is unit length and on the outer side of its triangle.
void ExpectOutwardNormals(const TriangleMesh& mesh, std::string_view name)
{
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const Vec3& a = mesh.positions[mesh.indices[3 * t]];
        const Vec3& b = mesh.positions[mesh.indices[3 * t + 1]];
        const Vec3& c = mesh.positions[mesh.indices[3 * t + 2]];
        const Vec3 face = Normalize(Cross(b - a, c - a));
        for (std::size_t k = 0; k < 3; ++k)
        {
            const Vec3& n = mesh.normals[3 * t + k];
            ASSERT_NEAR(Length(n), 1.0f, 1e-4f) << name << " triangle " << t;
            ASSERT_GT(Dot(n, face), 0.0f) << name << " triangle " << t;
        }
    }
}

double AnalyticVolume(const PrimitiveParams& p)
{
    const double r = double{p.radius};
    const double h = double{p.height};
    const double t = double{p.tube};
    const double box = double{p.size.x} * double{p.size.y} * double{p.size.z};
    const double n = p.steps;
    switch (p.shape)
    {
    case PrimitiveShape::Box:
        return box;
    case PrimitiveShape::Wedge:
        return box / 2.0;
    case PrimitiveShape::Stairs:
        return box * (n + 1.0) / (2.0 * n);
    case PrimitiveShape::Sphere:
        return 4.0 / 3.0 * kPi * r * r * r;
    case PrimitiveShape::Cylinder:
        return kPi * r * r * h;
    case PrimitiveShape::Cone:
        return kPi * r * r * h / 3.0;
    case PrimitiveShape::Capsule:
        return kPi * r * r * (h - 2.0 * r) + 4.0 / 3.0 * kPi * r * r * r;
    case PrimitiveShape::Torus:
        return 2.0 * kPi * kPi * r * t * t;
    case PrimitiveShape::Plane:
        break;
    }
    return 0.0;
}

constexpr PrimitiveShape kClosedShapes[] = {
    PrimitiveShape::Box,     PrimitiveShape::Sphere, PrimitiveShape::Cylinder, PrimitiveShape::Cone,
    PrimitiveShape::Capsule, PrimitiveShape::Wedge,  PrimitiveShape::Stairs,   PrimitiveShape::Torus,
};

} // namespace

TEST(Primitives, ClosedShapes_AreWatertightWithEulerCharacteristic)
{
    for (const PrimitiveShape shape : kClosedShapes)
    {
        PrimitiveParams params = Shape(shape);
        params.height = 2.0f;
        for (const std::uint32_t steps : {1u, 2u, 5u})
        {
            params.steps = steps;
            const TriangleMesh mesh = Make(params);
            ASSERT_TRUE(ValidateMesh(mesh).has_value()) << ToString(shape);
            EXPECT_TRUE(IsClosedManifold(mesh)) << ToString(shape) << " steps " << steps;
            EXPECT_EQ(EulerCharacteristic(mesh), shape == PrimitiveShape::Torus ? 0 : 2) << ToString(shape);
            ExpectOutwardNormals(mesh, ToString(shape));
            if (shape != PrimitiveShape::Stairs)
            {
                break;
            }
        }
    }
}

TEST(Primitives, MinimalDivisions_StayWatertight)
{
    for (const PrimitiveShape shape : kClosedShapes)
    {
        PrimitiveParams params = Shape(shape);
        params.height = 2.0f;
        params.segments = 3;
        params.rings = shape == PrimitiveShape::Sphere ? 2 : shape == PrimitiveShape::Torus ? 3 : 1;
        params.steps = 1;
        const TriangleMesh mesh = Make(params);
        EXPECT_TRUE(IsClosedManifold(mesh)) << ToString(shape);
        EXPECT_GT(MeshVolume(mesh), 0.0) << ToString(shape);
    }
}

TEST(Primitives, Volume_MatchesAnalytic)
{
    for (const PrimitiveShape shape : kClosedShapes)
    {
        PrimitiveParams params = Shape(shape);
        params.size = {2.0f, 1.5f, 3.0f};
        params.height = 2.5f;
        params.segments = 128;
        params.rings = 64;
        const double expected = AnalyticVolume(params);
        const double volume = MeshVolume(Make(params));
        // Polygonal shapes are exact; curved ones are inscribed, so slightly smaller.
        EXPECT_LE(volume, expected * (1.0 + 1e-5)) << ToString(shape);
        EXPECT_NEAR(volume, expected, expected * 3e-3) << ToString(shape);
    }
}

TEST(Primitives, Box_IsCentredWithOneSlotPerFace)
{
    PrimitiveParams params = Shape(PrimitiveShape::Box);
    params.size = {2.0f, 4.0f, 6.0f};
    const TriangleMesh mesh = Make(params);
    EXPECT_EQ(mesh.positions.size(), 8u);
    EXPECT_EQ(mesh.TriangleCount(), 12u);
    EXPECT_EQ(mesh.SlotCount(), 6u);
    EXPECT_EQ(PrimitiveSlots(PrimitiveShape::Box).size(), 6u);
    const MeshBounds bounds = BoundsOf(mesh);
    INTERI_EXPECT_VEC_NEAR(bounds.min, Vec3(-1.0f, -2.0f, -3.0f), 0.0f);
    INTERI_EXPECT_VEC_NEAR(bounds.max, Vec3(1.0f, 2.0f, 3.0f), 0.0f);
    // The +Y face's triangles carry slot 2 and face up.
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        if (mesh.slots[t] == 2)
        {
            INTERI_EXPECT_VEC_NEAR(mesh.normals[3 * t], Vec3(0.0f, 1.0f, 0.0f), 0.0f);
        }
    }
}

TEST(Primitives, SlotCounts_MatchFaceGroups)
{
    for (const PrimitiveShape shape : kClosedShapes)
    {
        PrimitiveParams params = Shape(shape);
        params.height = 2.0f;
        EXPECT_EQ(Make(params).SlotCount(), PrimitiveSlots(shape).size()) << ToString(shape);
    }
}

TEST(Primitives, Plane_IsOpenAndFacesUp)
{
    PrimitiveParams params = Shape(PrimitiveShape::Plane);
    params.size = {4.0f, 0.0f, 2.0f}; // size.y unused
    const TriangleMesh mesh = Make(params);
    EXPECT_FALSE(IsClosed(PrimitiveShape::Plane));
    EXPECT_FALSE(IsClosedManifold(mesh));
    EXPECT_EQ(mesh.TriangleCount(), 2u);
    ExpectOutwardNormals(mesh, "Plane");
    INTERI_EXPECT_VEC_NEAR(mesh.normals[0], Vec3(0.0f, 1.0f, 0.0f), 0.0f);
}

TEST(Primitives, InvalidParams_Fail)
{
    PrimitiveParams sphere = Shape(PrimitiveShape::Sphere);
    sphere.segments = 2;
    EXPECT_FALSE(GeneratePrimitive(sphere).has_value());
    sphere.segments = 2000;
    EXPECT_FALSE(GeneratePrimitive(sphere).has_value());
    PrimitiveParams box = Shape(PrimitiveShape::Box);
    box.size.y = 0.0f;
    EXPECT_FALSE(GeneratePrimitive(box).has_value());
    box.size.y = std::nanf("");
    EXPECT_FALSE(GeneratePrimitive(box).has_value());
    PrimitiveParams capsule = Shape(PrimitiveShape::Capsule);
    capsule.height = 1.0f; // == diameter
    EXPECT_FALSE(GeneratePrimitive(capsule).has_value());
    PrimitiveParams torus = Shape(PrimitiveShape::Torus);
    torus.tube = 0.5f;
    EXPECT_FALSE(GeneratePrimitive(torus).has_value());
}

TEST(Primitives, Component_ParamsRoundTrip)
{
    PrimitiveParams params = Shape(PrimitiveShape::Stairs);
    params.steps = 7;
    params.size = {1.0f, 2.0f, 3.0f};
    auto back = ParamsOf(MakePrimitiveComponent(params));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->shape, PrimitiveShape::Stairs);
    EXPECT_EQ(back->steps, 7u);
    PrimitiveComponent bad;
    bad.shape.name = "Blob";
    EXPECT_FALSE(ParamsOf(bad).has_value());
    for (const std::string& name : PrimitiveShapeNames())
    {
        EXPECT_EQ(ToString(*ParsePrimitiveShape(name)), name);
    }
}

TEST(Primitives, Reflection_ShapeIsAnEnumWithOptions)
{
    values::ReflectionRegistry registry;
    RegisterPrimitiveReflection(registry);
    const values::TypeReflection* type = registry.Find(PrimitiveComponent::kComponentName);
    ASSERT_NE(type, nullptr);
    const values::FieldInfo* shape = type->Find("shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ(shape->type, values::ValueType::Enum);
    EXPECT_EQ(shape->options.size(), kPrimitiveShapeCount);
}

TEST(MeshAsset, WriteThenRead_RoundTrips)
{
    const TriangleMesh mesh = Make(Shape(PrimitiveShape::Cylinder));
    const std::vector<std::byte> bytes = WriteMeshAsset(mesh);
    auto read = ReadMeshAsset(bytes);
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, mesh);

    MemoryFileSystem files;
    ASSERT_TRUE(files.WriteFile("meshes/cyl.imesh", bytes).has_value());
    auto loaded = LoadMeshAsset(files, "meshes/cyl.imesh");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, mesh);
}

TEST(MeshAsset, FlippedByte_IsCorrupt)
{
    std::vector<std::byte> bytes = WriteMeshAsset(Make(Shape(PrimitiveShape::Box)));
    bytes[bytes.size() / 2] ^= std::byte{0x40};
    auto read = ReadMeshAsset(bytes);
    ASSERT_FALSE(read.has_value());
    EXPECT_EQ(read.error().code, ErrorCode::Corrupt);
}

TEST(MeshAsset, InconsistentArrays_FailValidation)
{
    TriangleMesh mesh = Make(Shape(PrimitiveShape::Box));
    mesh.indices[4] = 99;
    EXPECT_FALSE(ValidateMesh(mesh).has_value());
    EXPECT_FALSE(ReadMeshAsset(WriteMeshAsset(mesh)).has_value());
}

TEST(MeshAsset, UvsAndTangents_RoundTrip)
{
    TriangleMesh mesh = Make(Shape(PrimitiveShape::Box));
    for (std::size_t i = 0; i < mesh.indices.size(); ++i)
    {
        mesh.uvs.push_back({.u = static_cast<float>(i) * 0.25f, .v = 1.0f - static_cast<float>(i) * 0.125f});
        mesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, i % 2 == 0 ? 1.0f : -1.0f);
    }
    auto read = ReadMeshAsset(WriteMeshAsset(mesh));
    ASSERT_TRUE(read.has_value()) << read.error().message;
    EXPECT_EQ(*read, mesh);
}

TEST(MeshAsset, UvCountMismatch_FailsValidation)
{
    TriangleMesh mesh = Make(Shape(PrimitiveShape::Box));
    mesh.uvs.resize(mesh.indices.size() - 1);
    EXPECT_FALSE(ValidateMesh(mesh).has_value());
    mesh.uvs.clear();
    mesh.tangents.resize(2);
    EXPECT_FALSE(ValidateMesh(mesh).has_value());
}

TEST(MeshAsset, NonFiniteUv_FailsValidation)
{
    TriangleMesh mesh = Make(Shape(PrimitiveShape::Box));
    mesh.uvs.assign(mesh.indices.size(), TexCoord{});
    mesh.uvs[3].u = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ValidateMesh(mesh).has_value());
}

TEST(MeshAsset, SlotColor_ClampsToTheLastColor)
{
    values::ValueList colors{.element = values::ValueType::Color, .items = {}};
    EXPECT_EQ(SlotColor(colors, 3).r, 0.6f);
    colors.items.emplace_back(values::Color{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f});
    colors.items.emplace_back(values::Color{.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f});
    EXPECT_EQ(SlotColor(colors, 0).r, 1.0f);
    EXPECT_EQ(SlotColor(colors, 5).g, 1.0f);
}
