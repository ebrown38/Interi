// SPDX-License-Identifier: MIT
#include <interi/hotreload/ModuleDocs.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <memory>

namespace interi::hotreload
{
namespace
{

bool Contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

TEST(ModuleDocs, ManifestOnly_ListsMetadataValuesAndTables)
{
    const std::vector<sdk::ModuleManifest> manifests = interi::testing::LoadSdkTestManifests();
    const sdk::ModuleManifest& a = *std::ranges::find(manifests, "com.test.a", &sdk::ModuleManifest::id);
    auto page = GenerateModuleDocs(a, nullptr, nullptr);
    ASSERT_TRUE(page.has_value()) << page.error().message;
    EXPECT_TRUE(Contains(*page, "# Test Module A"));
    EXPECT_TRUE(Contains(*page, "`com.test.a` · version 1.2.0 · Module"));
    EXPECT_TRUE(Contains(*page, "**Dependencies:** `com.test.ext ^1`"));
    EXPECT_TRUE(Contains(*page, "## Values"));
    EXPECT_TRUE(Contains(*page, "| `com.test.a:JumpHeight` | float |"));
    EXPECT_TRUE(Contains(*page, "FireMode"));
    EXPECT_TRUE(Contains(*page, "Single, Auto"));
    EXPECT_TRUE(Contains(*page, "## Data Tables"));
    EXPECT_TRUE(Contains(*page, "### `com.test.a:Guns` (version 2)"));
    EXPECT_FALSE(Contains(*page, "## Events")) << "events come from a loaded module";
}

TEST(ModuleDocs, Registries_ListWhatTheModuleProvidesIncludingOverrides)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules(graph, interi::testing::kSdkTestEngineContracts);
    chain::ChainRegistry chains(modules);
    sdk::HookRegistry hooks(modules);

    chain::EventDefinition fire;
    fire.tooltip = "Fires the weapon.";
    fire.category = "Combat";
    chain::EventParam damage;
    damage.name = "damage";
    damage.schema.type = values::ValueType::Float;
    damage.schema.defaultValue = 5.0f;
    fire.inputs = {damage};
    fire.outputs = {{.name = "Hit", .payload = {{.name = "target", .type = values::ValueType::EntityRef}}}};
    fire.implementation = std::make_shared<const chain::EventImpl>([](chain::EventCall&) {});
    ASSERT_TRUE(chains.Events().Provide("com.test.a", "com.test.a:Fire", fire).has_value());
    ASSERT_TRUE(chains.Events().Override("com.test.b", "com.test.a:Fire", fire).has_value());
    ASSERT_TRUE(hooks
                    .Declare<int(int)>("com.test.a", "com.test.a:Damage",
                                       [](const sdk::HookNext<int(int)>&, int x) { return x; })
                    .has_value());

    const sdk::ModuleManifest& a = graph.Get("com.test.a")->manifest;
    auto page = GenerateModuleDocs(a, &chains, &hooks);
    ASSERT_TRUE(page.has_value()) << page.error().message;
    EXPECT_TRUE(Contains(*page, "## Events"));
    EXPECT_TRUE(Contains(*page, "### `com.test.a:Fire`"));
    EXPECT_TRUE(Contains(*page, "Fires the weapon."));
    EXPECT_TRUE(Contains(*page, "| `damage` | float | 5"));
    EXPECT_TRUE(Contains(*page, "Outputs: `Hit` (target)"));
    EXPECT_TRUE(Contains(*page, "## Hook points"));
    EXPECT_TRUE(Contains(*page, "- `com.test.a:Damage`: `int(int)`"));
    EXPECT_FALSE(Contains(*page, "Overrides the lower module's event")) << "A's own entry isn't an override";

    const sdk::ModuleManifest& b = graph.Get("com.test.b")->manifest;
    auto overridePage = GenerateModuleDocs(b, &chains, &hooks);
    ASSERT_TRUE(overridePage.has_value());
    EXPECT_TRUE(Contains(*overridePage, "Overrides the lower module's event"));
    EXPECT_TRUE(Contains(*overridePage, "**Overrides:** `com.test.a:Fire ^1.2`"));
}

TEST(ModuleDocs, SameInput_SamePage)
{
    const sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    const sdk::ModuleManifest& a = graph.Get("com.test.a")->manifest;
    EXPECT_EQ(*GenerateModuleDocs(a, nullptr, nullptr), *GenerateModuleDocs(a, nullptr, nullptr));
}

TEST(ModuleDocs, ContentOnlyModule_SaysSo)
{
    sdk::ModuleManifest manifest;
    manifest.id = "com.test.content";
    manifest.name = "Content";
    auto page = GenerateModuleDocs(manifest, nullptr, nullptr);
    ASSERT_TRUE(page.has_value());
    EXPECT_TRUE(Contains(*page, "none (content only)"));
}

} // namespace
} // namespace interi::hotreload
