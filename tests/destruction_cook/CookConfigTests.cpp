// SPDX-License-Identifier: MIT
#include <interi/destruction_cook/CookConfig.h>

#include <gtest/gtest.h>

namespace
{

using namespace interi::destruction;
using interi::ErrorCode;

constexpr std::string_view kConfig = R"({
  "materialsFile": "materials.json",
  "groundHeight": 0.5,
  "defaults": { "material": "concrete", "levels": [ { "pattern": "uniform", "count": 5, "seed": 9 } ] },
  "surfaceMaterials": { "Glass": { "material": "glass" } },
  "objects": {
    "Tower": { "levels": [ { "pattern": "clustered", "count": 20, "impactPoint": [1, 2, 3], "clusterRadius": 2 },
                           { "pattern": "grid", "gridCellSize": [0.5, 0.5, 2], "jitter": 0.1 } ] },
    "Terrain": { "anchor": true },
    "Window": { "material": "steel" }
  },
  "hull": { "maxConcavity": 0.3, "maxDepth": 2 },
  "contact": { "distanceTolerance": 0.002 }
})";

} // namespace

TEST(CookConfig, FullConfig_Parsed)
{
    const auto config = ParseCookConfig(kConfig);
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_EQ(config->materialsFile, "materials.json");
    EXPECT_EQ(config->groundHeight, 0.5f);
    ASSERT_EQ(config->defaults.levels.size(), 1u);
    EXPECT_EQ(config->defaults.levels[0].count, 5u);
    EXPECT_EQ(config->defaults.levels[0].randomSeed, 9u);
    EXPECT_EQ(config->hull.maxConcavity, 0.3);
    EXPECT_EQ(config->hull.maxDepth, 2u);
    EXPECT_EQ(config->contact.distanceTolerance, 0.002);
    EXPECT_TRUE(config->warnings.empty());
}

TEST(CookConfig, SettingsFor_NodeOverridesMaterialOverridesDefaults)
{
    const auto config = ParseCookConfig(kConfig);
    ASSERT_TRUE(config.has_value());

    const auto plain = config->SettingsFor("Wall", "Concrete");
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->material, "concrete");
    EXPECT_EQ(plain->levels.size(), 1u);

    const auto glass = config->SettingsFor("Pane", "Glass");
    EXPECT_EQ(glass->material, "glass");

    const auto window = config->SettingsFor("Window", "Glass"); // node entry wins
    EXPECT_EQ(window->material, "steel");

    const auto tower = config->SettingsFor("Tower", "");
    ASSERT_EQ(tower->levels.size(), 2u);
    EXPECT_EQ(tower->levels[0].pattern, SeedPattern::Clustered);
    EXPECT_EQ(tower->levels[0].impactPoint, interi::Vec3(1, 2, 3));
    EXPECT_EQ(tower->levels[1].pattern, SeedPattern::Grid);
    EXPECT_EQ(tower->levels[1].gridCellSize, interi::Vec3(0.5f, 0.5f, 2.0f));

    EXPECT_TRUE(config->SettingsFor("Terrain", "")->anchor);
}

TEST(CookConfig, UnknownField_Warns)
{
    const auto config = ParseCookConfig(R"({ "defaults": { "colour": 1 }, "extra": true })");
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->warnings.size(), 2u);
}

TEST(CookConfig, BadValues_Rejected)
{
    for (const char* json :
         {R"({ "defaults": { "levels": [ { "pattern": "spiral" } ] } })",
          R"({ "defaults": { "levels": [ { "count": -3 } ] } })",
          R"({ "defaults": { "levels": [ { "jitter": 2 } ] } })", R"({ "objects": { "A": { "anchor": "yes" } } })",
          R"({ "hull": { "maxDepth": 99 } })", R"({ "groundHeight": "low" })", R"([1, 2])", R"({ broken )"})
    {
        const auto config = ParseCookConfig(json);
        ASSERT_FALSE(config.has_value()) << json;
        EXPECT_EQ(config.error().code, ErrorCode::InvalidArgument) << json;
    }
}

TEST(CookConfig, Default_OneUniformLevel)
{
    const CookConfig config = DefaultCookConfig();
    ASSERT_EQ(config.defaults.levels.size(), 1u);
    EXPECT_EQ(config.defaults.levels[0].count, 8u);
    EXPECT_FALSE(config.groundHeight.has_value());
}

TEST(CookConfig, PaintAndVolumes_Parsed)
{
    const auto config = ParseCookConfig(R"({
      "defaults": { "levels": [ { "pattern": "painted", "count": 3,
                                  "strokes": [ { "centre": [1, 2, 3], "radius": 0.5, "density": 80 } ] } ] },
      "objects": { "Wall": { "materialVolumes": [
          { "shape": "sphere", "centre": [0, 1, 0], "radius": 0.75, "material": "steel" },
          { "centre": [2, 1, 0], "halfExtents": [1, 2, 3], "material": "wood" } ] } }
    })");
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_TRUE(config->warnings.empty());
    const SeedSettings& level = config->defaults.levels.at(0);
    EXPECT_EQ(level.pattern, SeedPattern::Painted);
    ASSERT_EQ(level.strokes.size(), 1u);
    EXPECT_EQ(level.strokes[0].density, 80.0f);
    const auto wall = config->SettingsFor("Wall", "");
    ASSERT_TRUE(wall.has_value());
    ASSERT_EQ(wall->materialVolumes.size(), 2u);
    EXPECT_EQ(wall->materialVolumes[0].volume.shape, MaterialVolume::Shape::Sphere);
    EXPECT_EQ(wall->materialVolumes[0].volume.radius, 0.75f);
    EXPECT_EQ(wall->materialVolumes[1].volume.shape, MaterialVolume::Shape::Box);
    EXPECT_EQ(wall->materialVolumes[1].material, "wood");
    EXPECT_EQ(wall->materialVolumes[1].volume.halfExtents.z, 3.0f);
}

TEST(CookConfig, BadPaintAndVolumes_Rejected)
{
    for (const char* json : {
             R"({ "defaults": { "levels": [ { "strokes": {} } ] } })",
             R"({ "defaults": { "levels": [ { "strokes": [ { "radius": 0 } ] } ] } })",
             R"({ "defaults": { "materialVolumes": [ { "shape": "box" } ] } })",
             R"({ "defaults": { "materialVolumes": [ { "shape": "cone", "material": "steel" } ] } })",
             R"({ "objects": { "A": { "materialVolumes": [ { "material": "steel", "halfExtents": [0, 1, 1] } ] } } })",
         })
    {
        const auto config = ParseCookConfig(json);
        ASSERT_FALSE(config.has_value()) << json;
        EXPECT_EQ(config.error().code, ErrorCode::InvalidArgument);
    }
}

TEST(CookConfig, Write_RoundTrips)
{
    auto config = ParseCookConfig(kConfig);
    ASSERT_TRUE(config.has_value());
    ObjectCookSettings wall = config->defaults;
    SeedSettings painted;
    painted.pattern = SeedPattern::Painted;
    painted.count = 7;
    painted.randomSeed = 1234567890123ull;
    painted.strokes.push_back({.centre = {0.1f, 0.2f, 0.3f}, .radius = 0.4f, .density = 55.5f});
    wall.levels = {painted};
    wall.minPieceVolume = 0.002;
    wall.materialVolumes.push_back(
        {.volume = {.shape = MaterialVolume::Shape::Sphere, .centre = {1, 2, 3}, .radius = 0.3f}, .material = "steel"});
    config->SetObjectSettings("Wall", wall);

    const std::string written = WriteCookConfig(*config);
    const auto reread = ParseCookConfig(written);
    ASSERT_TRUE(reread.has_value()) << reread.error().message << "\n" << written;
    EXPECT_TRUE(reread->warnings.empty()) << written;
    EXPECT_EQ(reread->materialsFile, "materials.json");
    EXPECT_EQ(reread->groundHeight, 0.5f);
    EXPECT_EQ(reread->hull.maxDepth, 2u);
    EXPECT_EQ(reread->contact.distanceTolerance, 0.002);
    EXPECT_EQ(WriteCookConfig(*reread), written); // stable

    const auto tower = reread->SettingsFor("Tower", "");
    ASSERT_TRUE(tower.has_value());
    ASSERT_EQ(tower->levels.size(), 2u);
    EXPECT_EQ(tower->levels[0].pattern, SeedPattern::Clustered);
    EXPECT_EQ(tower->levels[0].impactPoint.z, 3.0f);
    const auto window = reread->SettingsFor("Window", "Glass");
    ASSERT_TRUE(window.has_value());
    EXPECT_EQ(window->material, "steel");

    const auto rereadWall = reread->SettingsFor("Wall", "");
    ASSERT_TRUE(rereadWall.has_value());
    ASSERT_EQ(rereadWall->levels.size(), 1u);
    EXPECT_EQ(rereadWall->levels[0].randomSeed, 1234567890123ull);
    ASSERT_EQ(rereadWall->levels[0].strokes.size(), 1u);
    EXPECT_EQ(rereadWall->levels[0].strokes[0].density, 55.5f);
    EXPECT_EQ(rereadWall->minPieceVolume, 0.002);
    ASSERT_EQ(rereadWall->materialVolumes.size(), 1u);
    EXPECT_EQ(rereadWall->materialVolumes[0].volume.centre.y, 2.0f);
    EXPECT_EQ(rereadWall->materialVolumes[0].material, "steel");
}
