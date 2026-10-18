// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/Prefab.h>
#include <interi/scene/Primitives.h>
#include <interi/scene/SceneComponents.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace interi;
using namespace interi::scene;

namespace
{

// A crate (root, id 1) with a lid (id 2) one metre up.
constexpr std::string_view kCrate = R"json({"version": 2, "objects": [
  {"id": 1, "components": {"interi.Name": {"name": "Crate"}, "interi.Parent": {"parent": 0},
                           "interi.Primitive": {"shape": "Box", "size": [1.0, 1.0, 1.0]}}},
  {"id": 2, "components": {"interi.Name": {"name": "Lid"}, "interi.Parent": {"parent": 1},
                           "interi.LocalTransform": {"position": [0.0, 1.0, 0.0]},
                           "interi.Primitive": {"shape": "Wedge"}}}]})json";

// Object 5 is an instance of the crate at x = 10, with the lid's shape overridden.
constexpr std::string_view kScene = R"json({"version": 2, "objects": [
  {"id": 5, "components": {"interi.Name": {"name": "My crate"}, "interi.Parent": {"parent": 0},
     "interi.LocalTransform": {"position": [10.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]},
     "interi.PrefabInstance": {"prefab": "prefabs/crate.prefab.json",
                               "overrides": ["2/interi.Primitive/shape=\"Sphere\""]}}}]})json";

class PrefabTest : public ::testing::Test
{
protected:
    PrefabTest()
    {
        RegisterSceneReflection(m_reflection);
        RegisterPrimitiveReflection(m_reflection);
        RegisterPrefabReflection(m_reflection);
        Write("prefabs/crate.prefab.json", kCrate);
    }

    void Write(std::string_view path, std::string_view text)
    {
        ASSERT_TRUE(m_files.WriteFile(path, std::as_bytes(std::span(text))).has_value());
    }

    [[nodiscard]] static std::string Field(const SceneObject& object, std::string_view component,
                                           std::string_view field)
    {
        const SceneComponent* found = object.Find(component);
        if (found == nullptr)
        {
            return {};
        }
        for (const auto& [name, json] : found->fields)
        {
            if (name == field)
            {
                return CanonicalJson(json);
            }
        }
        return {};
    }

    MemoryFileSystem m_files;
    PrefabLibrary m_library{m_files};
    values::ReflectionRegistry m_reflection;
};

} // namespace

TEST(PrefabOverride, FormatParse_RoundTrips)
{
    const PrefabOverride entry{.source = 7, .component = "com.a:Thing", .field = "power", .json = "[1,2]"};
    auto parsed = ParseOverride(FormatOverride(entry));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, entry);
    EXPECT_FALSE(ParseOverride("7/x=1").has_value());
    EXPECT_FALSE(ParseOverride("a/b/c=1").has_value());
    EXPECT_FALSE(ParseOverride("7/b/c={").has_value());
}

TEST(PrefabRoot, NeedsExactlyOneRoot)
{
    EXPECT_EQ(*PrefabRoot(*ParseScene(kCrate)), 1u);
    EXPECT_FALSE(PrefabRoot(*ParseScene(R"({"version": 2, "objects": [{"id": 1}, {"id": 2}]})")).has_value());
    EXPECT_FALSE(PrefabRoot(*ParseScene(R"({"version": 2, "objects": []})")).has_value());
    EXPECT_FALSE(
        PrefabRoot(
            *ParseScene(R"({"version": 2, "objects": [{"id": 1, "components": {"interi.Parent": {"parent": 9}}}]})"))
            .has_value());
    EXPECT_FALSE(PrefabRoot(*ParseScene(R"({"version": 2, "objects": [{"id": 70000}]})")).has_value());
}

TEST_F(PrefabTest, Expand_BuildsObjectsWithDerivedIdsAndOverrides)
{
    SceneDocument document = *ParseScene(kScene);
    std::vector<std::string> warnings;
    ExpandPrefabs(document, m_library.Loader(), warnings);
    EXPECT_TRUE(warnings.empty());
    ASSERT_EQ(document.objects.size(), 2u);
    const SceneObject& root = document.objects[0];
    EXPECT_EQ(root.id, 5u);
    EXPECT_EQ(Field(root, "interi.Name", "name"), "\"My crate\"");
    EXPECT_EQ(Field(root, "interi.Primitive", "shape"), "\"Box\""); // from the prefab
    EXPECT_EQ(Field(root, "interi.PrefabLink", "source"), "1");
    const SceneObject& lid = document.objects[1];
    EXPECT_EQ(lid.id, PrefabObjectId(5, 2));
    EXPECT_TRUE(IsPrefabObjectId(lid.id));
    EXPECT_EQ(Field(lid, "interi.Parent", "parent"), "5");
    EXPECT_EQ(Field(lid, "interi.Primitive", "shape"), "\"Sphere\""); // overridden

    World world;
    auto instantiated = InstantiateScene(document, world, m_reflection);
    ASSERT_TRUE(instantiated.has_value()) << instantiated.error().message;
    INTERI_EXPECT_VEC_NEAR(world.Get<Transform>(instantiated->entities.at(lid.id))->position, Vec3(10, 1, 0), 0.0f);
}

TEST_F(PrefabTest, CollapseAfterEdit_RecordsOnlyDifferences)
{
    SceneDocument document = *ParseScene(kScene);
    std::vector<std::string> warnings;
    ExpandPrefabs(document, m_library.Loader(), warnings);

    // Capture through a world, as an editor save does, after moving the lid.
    World world;
    auto instantiated = InstantiateScene(document, world, m_reflection);
    ASSERT_TRUE(instantiated.has_value());
    world.GetMut<LocalTransformComponent>(instantiated->entities.at(PrefabObjectId(5, 2)))->position = {0, 2, 0};
    SceneDocument captured = CaptureScene(world, m_reflection, &instantiated->kept);
    CollapsePrefabs(captured, m_library.Loader(), m_reflection);

    ASSERT_EQ(captured.objects.size(), 1u);
    const SceneObject& root = captured.objects[0];
    EXPECT_EQ(root.Find("interi.Primitive"), nullptr);
    EXPECT_EQ(root.Find("interi.PrefabLink"), nullptr);
    const std::string overrides = Field(root, "interi.PrefabInstance", "overrides");
    EXPECT_NE(overrides.find(R"(2/interi.Primitive/shape=\"Sphere\")"), std::string::npos) << overrides;
    EXPECT_NE(overrides.find("2/interi.LocalTransform/position=[0.0,2.0,0.0]"), std::string::npos) << overrides;
    EXPECT_EQ(overrides.find("rotation"), std::string::npos) << overrides; // unchanged fields aren't overrides

    // Expanding the collapsed scene gives the edited state back.
    ExpandPrefabs(captured, m_library.Loader(), warnings);
    EXPECT_EQ(Field(*captured.Find(PrefabObjectId(5, 2)), "interi.LocalTransform", "position"), "[0.0,2.0,0.0]");
}

TEST_F(PrefabTest, PrefabEdit_ReachesInstancesExceptOverrides)
{
    Write("prefabs/crate.prefab.json", R"json({"version": 2, "objects": [
      {"id": 1, "components": {"interi.Name": {"name": "Crate"}, "interi.Primitive": {"shape": "Cylinder"}}},
      {"id": 2, "components": {"interi.Parent": {"parent": 1}, "interi.Primitive": {"shape": "Cone"}}},
      {"id": 3, "components": {"interi.Parent": {"parent": 1}, "interi.Name": {"name": "New"}}}]})json");
    SceneDocument document = *ParseScene(kScene);
    std::vector<std::string> warnings;
    ExpandPrefabs(document, m_library.Loader(), warnings);
    ASSERT_EQ(document.objects.size(), 3u);
    EXPECT_EQ(Field(document.objects[0], "interi.Primitive", "shape"), "\"Cylinder\"");
    EXPECT_EQ(Field(*document.Find(PrefabObjectId(5, 2)), "interi.Primitive", "shape"), "\"Sphere\"");
    EXPECT_NE(document.Find(PrefabObjectId(5, 3)), nullptr);
}

TEST_F(PrefabTest, StaleOverrideAndMissingPrefab_Warn)
{
    SceneDocument document = *ParseScene(R"json({"version": 2, "objects": [
      {"id": 5, "components": {"interi.PrefabInstance": {"prefab": "prefabs/crate.prefab.json",
                                                          "overrides": ["9/interi.Name/name=\"x\""]}}},
      {"id": 6, "components": {"interi.PrefabInstance": {"prefab": "prefabs/missing.prefab.json"}}}]})json");
    std::vector<std::string> warnings;
    ExpandPrefabs(document, m_library.Loader(), warnings);
    EXPECT_EQ(warnings.size(), 2u);
    EXPECT_EQ(document.objects.size(), 3u); // 5 and its lid, 6 alone
}

TEST_F(PrefabTest, SpawnPrefabs_CreatesEachRequest)
{
    World world;
    Transform at;
    at.position = {0, 3, 0};
    const chain::SpawnRequest requests[] = {{.prefab = "prefabs/crate.prefab.json", .id = 100, .transform = at},
                                            {.prefab = "prefabs/missing.prefab.json", .id = 101, .transform = {}}};
    std::vector<std::string> warnings;
    const std::vector<ObjectId> spawned = SpawnPrefabs(requests, world, m_reflection, m_library.Loader(), warnings);
    EXPECT_EQ(spawned, (std::vector<ObjectId>{100}));
    EXPECT_EQ(warnings.size(), 1u);
    const HierarchyIndex index = BuildHierarchy(world);
    ASSERT_EQ(index.nodes.size(), 2u);
    EXPECT_EQ(world.Get<NameComponent>(index.nodes.at(100).entity)->name, "Crate");
    INTERI_EXPECT_VEC_NEAR(world.Get<Transform>(index.nodes.at(PrefabObjectId(100, 2)).entity)->position, Vec3(0, 4, 0),
                           0.0f);
}

TEST_F(PrefabTest, MakePrefab_RenumbersAndCentresTheRoot)
{
    SceneDocument document = *ParseScene(R"json({"version": 2, "objects": [
      {"id": 3, "components": {"interi.Name": {"name": "other"}}},
      {"id": 4, "components": {"interi.Name": {"name": "root"},
                               "interi.LocalTransform": {"position": [5.0, 0.0, 0.0]}}},
      {"id": 8, "components": {"interi.Parent": {"parent": 9}}},
      {"id": 9, "components": {"interi.Parent": {"parent": 4}}}]})json");
    auto prefab = MakePrefab(document, 4);
    ASSERT_TRUE(prefab.has_value()) << prefab.error().message;
    ASSERT_EQ(prefab->objects.size(), 3u);
    EXPECT_EQ(*PrefabRoot(*prefab), 1u);
    EXPECT_EQ(Field(prefab->objects[0], "interi.LocalTransform", "position"), "[0.0,0.0,0.0]");
    // Ids in id order after the root: 8 -> 2 (under 9 -> 3), 9 -> 3 (under the root).
    EXPECT_EQ(Field(prefab->objects[1], "interi.Parent", "parent"), "3");
    EXPECT_EQ(Field(prefab->objects[2], "interi.Parent", "parent"), "1");
    EXPECT_FALSE(MakePrefab(document, 99).has_value());
}
