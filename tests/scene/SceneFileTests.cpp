// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneFile.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace interi;
using namespace interi::scene;

namespace
{

struct CrateComponent
{
    static constexpr const char* kComponentName = "test.scene.Crate";
    float mass = 1.0f;
    std::int32_t count = 2;
    values::AssetRef mesh;
    values::ValueList tags{.element = values::ValueType::String, .items = {}};
};

values::ReflectionRegistry MakeReflection()
{
    values::ReflectionRegistry registry;
    RegisterSceneReflection(registry);
    registry.Register<CrateComponent>({
        values::Field("mass", &CrateComponent::mass),
        values::Field("count", &CrateComponent::count),
        values::Field("mesh", &CrateComponent::mesh),
        values::Field("tags", &CrateComponent::tags),
    });
    return registry;
}

constexpr std::string_view kSample = R"json({
  "version": 2,
  "objects": [
    { "id": 7, "components": {
        "interi.Name": { "name": "Lid" },
        "interi.Parent": { "parent": 3 },
        "interi.LocalTransform": { "position": [0, 1, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "test.scene.Other": { "anything": [1, 2, { "deep": true }] } } },
    { "id": 3, "components": {
        "interi.Name": { "name": "Crate" },
        "interi.LocalTransform": { "position": [5, 0, 0] },
        "test.scene.Crate": { "mass": 12.5, "mesh": "meshes/crate.imesh", "tags": ["wood", "loot"] } } }
  ]
})json";

} // namespace

TEST(SceneFile, Sample_ParsesSortedById)
{
    auto document = ParseScene(kSample);
    ASSERT_TRUE(document.has_value()) << document.error().message;
    EXPECT_TRUE(document->warnings.empty());
    ASSERT_EQ(document->objects.size(), 2u);
    EXPECT_EQ(document->objects[0].id, 3u);
    const SceneComponent* crate = document->objects[0].Find("test.scene.Crate");
    ASSERT_NE(crate, nullptr);
    ASSERT_EQ(crate->fields.size(), 3u);
    EXPECT_EQ(crate->fields[0].first, "mass");
    EXPECT_NE(document->Find(7), nullptr);
    EXPECT_EQ(document->Find(4), nullptr);
}

TEST(SceneFile, WriteThenParse_RoundTripsText)
{
    auto document = ParseScene(kSample);
    ASSERT_TRUE(document.has_value());
    const std::string written = WriteScene(*document);
    auto again = ParseScene(written);
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_EQ(*again, *document);
    EXPECT_EQ(WriteScene(*again), written);
}

TEST(SceneFile, Instantiate_AppliesFieldsDefaultsAndHierarchy)
{
    const values::ReflectionRegistry reflection = MakeReflection();
    auto document = ParseScene(kSample);
    ASSERT_TRUE(document.has_value());
    World world;
    auto scene = InstantiateScene(*document, world, reflection);
    ASSERT_TRUE(scene.has_value()) << scene.error().message;

    const Entity crate = scene->entities.at(3);
    const auto* component = world.Get<CrateComponent>(crate);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->mass, 12.5f);
    EXPECT_EQ(component->count, 2); // not in the file: the default
    EXPECT_EQ(component->mesh.path, "meshes/crate.imesh");
    ASSERT_EQ(component->tags.items.size(), 2u);
    EXPECT_EQ(world.Get<ParentComponent>(crate)->parent.id, 0u); // missing standard component: a root
    INTERI_EXPECT_VEC_NEAR(world.Get<Transform>(scene->entities.at(7))->position, Vec3(5.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_EQ(world.Get<NameComponent>(scene->entities.at(7))->name, "Lid");

    ASSERT_EQ(scene->kept.size(), 1u);
    EXPECT_EQ(scene->kept.at(7)[0].name, "test.scene.Other");
    EXPECT_EQ(scene->warnings.size(), 1u);
}

TEST(SceneFile, InstantiateThenCapture_KeepsUnknownComponents)
{
    const values::ReflectionRegistry reflection = MakeReflection();
    auto document = ParseScene(kSample);
    ASSERT_TRUE(document.has_value());
    World world;
    auto scene = InstantiateScene(*document, world, reflection);
    ASSERT_TRUE(scene.has_value());

    const SceneDocument captured = CaptureScene(world, reflection, &scene->kept);
    ASSERT_EQ(captured.objects.size(), 2u);
    EXPECT_EQ(*captured.objects[1].Find("test.scene.Other"), *document->objects[1].Find("test.scene.Other"));
    // The captured document has every field and standard component, and is a fixed point.
    EXPECT_EQ(captured.objects[0].Find("test.scene.Crate")->fields.size(), 4u);
    EXPECT_NE(captured.objects[0].Find("interi.Parent"), nullptr);
    EXPECT_EQ(captured.objects[0].Find("interi.Transform"), nullptr);

    World again;
    auto reloaded = InstantiateScene(*ParseScene(WriteScene(captured)), again, reflection);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_EQ(WriteScene(CaptureScene(again, reflection, &reloaded->kept)), WriteScene(captured));
}

TEST(SceneFile, Floats_RoundTripBitExactly)
{
    const values::ReflectionRegistry reflection = MakeReflection();
    World world;
    const Entity entity = CreateObject(world, 1, "x");
    (void)world.Add(entity, CrateComponent{.mass = 0.1f + 1e-7f, .count = -3, .mesh = {}, .tags = {}});
    const std::string text = WriteScene(CaptureScene(world, reflection));

    World again;
    auto scene = InstantiateScene(*ParseScene(text), again, reflection);
    ASSERT_TRUE(scene.has_value());
    EXPECT_EQ(again.Get<CrateComponent>(scene->entities.at(1))->mass, 0.1f + 1e-7f);
    EXPECT_EQ(again.Get<CrateComponent>(scene->entities.at(1))->count, -3);
}

TEST(SceneFile, BadField_FailsAndCreatesNothing)
{
    const values::ReflectionRegistry reflection = MakeReflection();
    auto document = ParseScene(R"json({"version": 2, "objects": [
        {"id": 1, "components": {"interi.Name": {"name": "ok"}}},
        {"id": 2, "components": {"test.scene.Crate": {"mass": "heavy"}}}]})json");
    ASSERT_TRUE(document.has_value());
    World world;
    auto scene = InstantiateScene(*document, world, reflection);
    ASSERT_FALSE(scene.has_value());
    EXPECT_NE(scene.error().message.find("object 2: test.scene.Crate.mass"), std::string::npos)
        << scene.error().message;
    EXPECT_EQ(world.EntityCount(), 0u);
}

TEST(SceneFile, IdInUse_Fails)
{
    const values::ReflectionRegistry reflection = MakeReflection();
    World world;
    (void)CreateObject(world, 1, "existing");
    auto scene = InstantiateScene(*ParseScene(R"({"version": 2, "objects": [{"id": 1}]})"), world, reflection);
    ASSERT_FALSE(scene.has_value());
    EXPECT_EQ(world.EntityCount(), 1u);
}

TEST(SceneFile, Malformed_ErrorsNameTheProblem)
{
    struct Case
    {
        std::string_view json;
        std::string_view message;
    };
    const Case cases[] = {
        {"[]", "not a JSON object"},
        {R"({"objects": []})", "\"version\""},
        {R"({"version": 1})", "level importer"},
        {R"({"version": 2, "objects": {}})", "\"objects\" must be an array"},
        {R"({"version": 2, "objects": [{"id": 0}]})", "object #0: \"id\""},
        {R"({"version": 2, "objects": [{"id": 4, "components": []}]})", "object 4: \"components\""},
        {R"({"version": 2, "objects": [{"id": 4, "components": {"a": 1}}]})", "component 'a'"},
        {R"({"version": 2, "objects": [{"id": 4}, {"id": 4}]})", "duplicate object id 4"},
    };
    for (const Case& c : cases)
    {
        auto document = ParseScene(c.json);
        ASSERT_FALSE(document.has_value()) << c.json;
        EXPECT_NE(document.error().message.find(c.message), std::string::npos) << document.error().message;
    }
    auto newer = ParseScene(R"({"version": 3})");
    ASSERT_FALSE(newer.has_value());
    EXPECT_EQ(newer.error().code, ErrorCode::VersionMismatch);
}

TEST(SceneFile, UnknownKeys_Warn)
{
    auto document = ParseScene(R"({"version": 2, "extra": 1, "objects": [{"id": 1, "note": "x"}]})");
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->warnings.size(), 2u);
}

TEST(SceneFile, Load_PrefixesPath)
{
    MemoryFileSystem files;
    const std::string_view text = R"({"version": 2, "objects": "x"})";
    ASSERT_TRUE(files.WriteFile("scenes/bad.scene.json", std::as_bytes(std::span(text))).has_value());
    auto document = LoadScene(files, "scenes/bad.scene.json");
    ASSERT_FALSE(document.has_value());
    EXPECT_TRUE(document.error().message.starts_with("scenes/bad.scene.json: ")) << document.error().message;
}
