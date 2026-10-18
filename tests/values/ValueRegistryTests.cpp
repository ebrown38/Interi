// SPDX-License-Identifier: MIT
// Values of the shared SDK test project (tests/sdk/modules): com.test.ext declares the shared value.Gravity
// (-9.81); com.test.a defines com.test.a:JumpHeight (1.2, 0..10), com.test.a:FireMode and provides value.Gravity
// (-20); com.test.b provides value.Gravity (-5, -50..0) and overrides com.test.a:JumpHeight (2.5, max 20).
#include <interi/values/ValueRegistry.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace interi;
using namespace interi::values;

namespace
{

class Values : public ::testing::Test
{
protected:
    void SetUp() override
    {
        for (const std::size_t index : graph.loadOrder)
        {
            Result<ModuleContent> content = LoadModuleContent(graph.modules[index].manifest);
            ASSERT_TRUE(content.has_value()) << content.error().message;
            EXPECT_TRUE(content->warnings.empty()) << content->warnings.front();
            ASSERT_TRUE(registry.AddModule(*content).has_value());
        }
    }

    [[nodiscard]] std::vector<std::string> Providers(std::string_view id) const
    {
        std::vector<std::string> providers;
        for (const sdk::ProvenanceEntry& entry : registry.Provenance(id))
        {
            providers.push_back(entry.provider);
        }
        return providers;
    }

    sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
    ValueRegistry registry{modules};
};

} // namespace

TEST_F(Values, Value_ProjectOverride_BeatsModuleDefault)
{
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 2.5f); // B's override of A's default
    ASSERT_TRUE(registry.SetProjectValue("com.test.a:JumpHeight", 4.0f).has_value());
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 4.0f);
    EXPECT_EQ(Providers("com.test.a:JumpHeight"), (std::vector<std::string>{"project", "com.test.b", "com.test.a"}));

    EXPECT_TRUE(registry.ClearProjectValue("com.test.a:JumpHeight"));
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 2.5f);
    EXPECT_FALSE(registry.ClearProjectValue("com.test.a:JumpHeight"));
}

TEST_F(Values, Value_SharedId_HigherModuleDefinitionWins)
{
    const ValueDefinition* gravity = registry.Definition("value.Gravity");
    ASSERT_NE(gravity, nullptr);
    EXPECT_EQ(registry.Get<float>("value.Gravity"), -5.0f); // B is above A and the Extension
    EXPECT_EQ(gravity->schema.meta.min, -50.0);             // its range too: nothing is merged
    EXPECT_TRUE(gravity->schema.meta.shared);
    EXPECT_EQ(Providers("value.Gravity"), (std::vector<std::string>{"com.test.b", "com.test.a", "com.test.ext"}));

    // A project value is checked against the winner's range.
    EXPECT_FALSE(registry.SetProjectValue("value.Gravity", 3.0f).has_value());
    EXPECT_TRUE(registry.SetProjectValue("value.Gravity", -30.0f).has_value());
}

TEST_F(Values, UnoverriddenValues_StayActive)
{
    EXPECT_EQ(registry.Get<EnumValue>("com.test.a:FireMode").value_or(EnumValue{}).name, "Single");
    EXPECT_EQ(Providers("com.test.a:FireMode"), std::vector<std::string>{"com.test.a"});
}

TEST_F(Values, TypedGet_Mismatch_Error)
{
    const Result<std::int32_t> wrong = registry.Get<std::int32_t>("com.test.a:JumpHeight");
    ASSERT_FALSE(wrong.has_value());
    EXPECT_EQ(wrong.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.Get<float>("com.test.a:Nothing").error().code, ErrorCode::NotFound);
    EXPECT_EQ(registry.SetProjectValue("com.test.a:Nothing", 1.0f).error().code, ErrorCode::NotFound);
    EXPECT_EQ(registry.SetProjectValue("com.test.a:JumpHeight", std::int32_t{1}).error().code,
              ErrorCode::InvalidArgument);
}

TEST_F(Values, Override_ChangingType_Error)
{
    ValueDefinition definition;
    definition.id = "com.test.a:FireMode";
    definition.isOverride = true;
    definition.schema.type = ValueType::Int;
    definition.schema.defaultValue = std::int32_t{1};
    sdk::ModuleSet set = modules;
    set.AddProvider(sdk::Provider{
        .id = "com.test.b",
        .kind = sdk::ModuleKind::Module,
        .version = {.major = 2, .minor = 0, .patch = 0, .prerelease = {}, .build = {}},
        .rank = 3,
        .overrides = {{.target = "com.test.a:FireMode", .version = sdk::ParseVersionRange("*").value()}}});
    ValueRegistry other(set);
    ModuleContent a = LoadModuleContent(graph.Get("com.test.a")->manifest).value();
    std::erase_if(a.values, [](const ValueDefinition& d) { return d.id == "value.Gravity"; });
    ASSERT_TRUE(other.AddModule(a).has_value());
    const Result<void> result = other.Add("com.test.b", definition);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);

    definition.schema.type = ValueType::Enum;
    definition.schema.options = {"Single", "Auto", "Burst"};
    definition.schema.defaultValue = EnumValue{.name = "Burst"};
    EXPECT_TRUE(other.Add("com.test.b", definition).has_value());
    EXPECT_EQ(other.Get<EnumValue>("com.test.a:FireMode").value_or(EnumValue{}).name, "Burst");
}

TEST_F(Values, SharedId_Undeclared_Error)
{
    ValueDefinition definition;
    definition.id = "value.Undeclared";
    definition.schema.defaultValue = 1.0f;
    const Result<void> result = registry.Add("com.test.a", definition);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST_F(Values, Override_Undeclared_Error)
{
    ValueDefinition definition;
    definition.id = "com.test.a:FireMode";
    definition.isOverride = true;
    definition.schema.type = ValueType::Enum;
    definition.schema.options = {"Single"};
    definition.schema.defaultValue = EnumValue{.name = "Single"};
    EXPECT_FALSE(registry.Add("com.test.b", definition).has_value()); // B's manifest doesn't declare it
}

TEST_F(Values, ModuleRemoved_ProjectValueInactiveThenBack)
{
    ASSERT_TRUE(registry.SetProjectValue("com.test.a:JumpHeight", 15.0f).has_value()); // fits B's max 20
    ValueRegistry::Entries b = registry.ExtractProvider("com.test.b");
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 1.2f) << "A's max is 10: the override is inactive";
    ASSERT_NE(registry.GetProjectValue("com.test.a:JumpHeight"), nullptr);
    EXPECT_FALSE(registry.GetProjectValue("com.test.a:JumpHeight")->active);
    const std::vector<std::string> warnings = registry.Revalidate();
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("com.test.a:JumpHeight"), std::string::npos);

    registry.Restore(std::move(b));
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 15.0f);
    EXPECT_TRUE(registry.Revalidate().empty());
}

TEST_F(Values, Subscribe_ReportsChanges)
{
    std::vector<std::string> changed;
    const sdk::Subscription subscription = registry.Subscribe([&](std::string_view id) { changed.emplace_back(id); });
    ASSERT_TRUE(registry.SetProjectValue("com.test.a:JumpHeight", 3.0f).has_value());
    (void)registry.ExtractProvider("com.test.b");
    EXPECT_NE(std::ranges::find(changed, "com.test.a:JumpHeight"), changed.end());
    EXPECT_NE(std::ranges::find(changed, "value.Gravity"), changed.end());
}

TEST_F(Values, LoadProjectValue_UncheckedButInactiveWhenInvalid)
{
    registry.LoadProjectValue("com.test.a:JumpHeight", std::string("tall"));
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 2.5f);
    registry.LoadProjectValue("com.test.a:JumpHeight", 3.0f);
    EXPECT_EQ(registry.Get<float>("com.test.a:JumpHeight"), 3.0f);
}
