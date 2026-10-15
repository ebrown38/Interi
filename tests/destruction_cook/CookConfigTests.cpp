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
