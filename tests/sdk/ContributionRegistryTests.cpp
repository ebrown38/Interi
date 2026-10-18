// SPDX-License-Identifier: MIT
#include <interi/sdk/ContributionRegistry.h>

#include "SdkTestModules.h"
#include <gtest/gtest.h>

#include <string>
#include <vector>

using interi::ErrorCode;
using interi::sdk::ContributionRegistry;
using interi::sdk::kEngineProviderId;
using interi::sdk::ModuleSet;

namespace
{

class Registry : public ::testing::Test
{
protected:
    interi::sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
    ContributionRegistry<std::string> registry{modules};
};

std::vector<std::string> Providers(const ContributionRegistry<std::string>& registry, std::string_view id)
{
    std::vector<std::string> providers;
    for (const auto& entry : registry.Provenance(id))
    {
        providers.push_back(entry.provider);
    }
    return providers;
}

} // namespace

TEST_F(Registry, SharedId_HigherModuleWinsOutright)
{
    // Registered bottom-up, as the host loads them; the result doesn't depend on the order.
    ASSERT_TRUE(registry.Provide("com.test.ext", "service.Greeter", "ext").has_value());
    ASSERT_TRUE(registry.Provide("com.test.b", "service.Greeter", "b").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "service.Greeter", "a").has_value());
    ASSERT_NE(registry.Resolve("service.Greeter"), nullptr);
    EXPECT_EQ(*registry.Resolve("service.Greeter"), "b"); // only the winner's definition is in effect

    // An engine-declared contract works the same way, the engine being the lowest provider.
    ASSERT_TRUE(registry.Provide(kEngineProviderId, "event.Damage", "engine").has_value());
    EXPECT_EQ(*registry.Resolve("event.Damage"), "engine");
    ASSERT_TRUE(registry.Provide("com.test.a", "event.Damage", "a").has_value());
    EXPECT_EQ(*registry.Resolve("event.Damage"), "a");
}

TEST_F(Registry, SharedId_Undeclared_Error)
{
    auto result = registry.Provide("com.test.a", "service.Unknown", "a");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
}

TEST_F(Registry, LocalIds_NeverCollide)
{
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a-fire").has_value());
    ASSERT_TRUE(registry.Provide("com.test.b", "com.test.b:Fire", "b-fire").has_value());
    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "a-fire");
    EXPECT_EQ(*registry.Resolve("com.test.b:Fire"), "b-fire");

    // Providing another module's local id is refused, even from a higher module: only an explicit override may.
    auto stolen = registry.Provide("com.test.b", "com.test.a:Fire", "b");
    ASSERT_FALSE(stolen.has_value());
    EXPECT_EQ(stolen.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "a-fire");
    // A module provides each of its ids once.
    EXPECT_FALSE(registry.Provide("com.test.a", "com.test.a:Fire", "again").has_value());
}

TEST_F(Registry, Override_ReplacesLowerLocalItem)
{
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value());
    ASSERT_TRUE(registry.Override("com.test.b", "com.test.a:Fire", "b").has_value());
    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "b");
    ASSERT_TRUE(registry.Override("com.test.plugin", "com.test.a:Fire", "plugin").has_value());
    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "plugin");
}

TEST_F(Registry, Override_Rules)
{
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Jump", "a").has_value());
    ASSERT_TRUE(registry.Provide("com.test.b", "com.test.b:Reload", "b").has_value());

    // Not declared in B's manifest.
    EXPECT_EQ(registry.Override("com.test.b", "com.test.a:Jump", "b").error().code, ErrorCode::InvalidArgument);
    // A is below B: a lower module can't override a higher one's item.
    interi::sdk::Provider lower = *modules.Find("com.test.a");
    lower.overrides.push_back({.target = "com.test.b:Reload", .version = {}});
    modules.AddProvider(lower);
    EXPECT_EQ(registry.Override("com.test.a", "com.test.b:Reload", "a").error().code, ErrorCode::InvalidArgument);
    // Shared ids aren't overridden, they're provided.
    EXPECT_EQ(registry.Override("com.test.b", "service.Greeter", "b").error().code, ErrorCode::InvalidArgument);
}

TEST_F(Registry, Override_TargetVersionOutOfRange_Error)
{
    // B declares "^1.2" for com.test.a; pretend 1.1.0 is loaded.
    interi::sdk::Provider old = *modules.Find("com.test.a");
    old.version = interi::sdk::ParseSemVer("1.1.0").value();
    modules.AddProvider(old);
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value());
    EXPECT_EQ(registry.Override("com.test.b", "com.test.a:Fire", "b").error().code, ErrorCode::VersionMismatch);
}

TEST_F(Registry, Plugin_OnlyOverrides)
{
    EXPECT_FALSE(registry.Provide("com.test.plugin", "com.test.plugin:Thing", "p").has_value());
    // A shared id nobody provides yet can't be introduced by a Plugin...
    EXPECT_FALSE(registry.Provide("com.test.plugin", "input.Jump", "p").has_value());
    // ...but one that exists can be taken over.
    ASSERT_TRUE(registry.Provide("com.test.a", "input.Jump", "a").has_value());
    ASSERT_TRUE(registry.Provide("com.test.plugin", "input.Jump", "p").has_value());
    EXPECT_EQ(*registry.Resolve("input.Jump"), "p");
}

TEST_F(Registry, UnoverriddenItems_StayActive)
{
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a-fire").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Jump", "a-jump").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "attr.Health", "a-health").has_value());
    ASSERT_TRUE(registry.Provide("com.test.b", "input.Jump", "b-jump").has_value());
    ASSERT_TRUE(registry.Override("com.test.b", "com.test.a:Fire", "b-fire").has_value());

    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "b-fire");
    EXPECT_EQ(*registry.Resolve("com.test.a:Jump"), "a-jump");
    EXPECT_EQ(*registry.Resolve("attr.Health"), "a-health");
    EXPECT_EQ(*registry.Resolve("input.Jump"), "b-jump");
    EXPECT_EQ(registry.Resolve("com.test.a:Missing"), nullptr);
    EXPECT_EQ(registry.Ids(),
              (std::vector<std::string>{"attr.Health", "com.test.a:Fire", "com.test.a:Jump", "input.Jump"}));
}

TEST_F(Registry, Provenance_ListsShadowed)
{
    ASSERT_TRUE(registry.Provide("com.test.ext", "service.Greeter", "ext").has_value());
    ASSERT_TRUE(registry.Provide("com.test.b", "service.Greeter", "b").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "service.Greeter", "a").has_value());
    ASSERT_TRUE(registry.Provide("com.test.plugin", "service.Greeter", "plugin").has_value());
    EXPECT_EQ(Providers(registry, "service.Greeter"),
              (std::vector<std::string>{"com.test.plugin", "com.test.b", "com.test.a", "com.test.ext"}));

    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value());
    ASSERT_TRUE(registry.Override("com.test.b", "com.test.a:Fire", "b").has_value());
    const auto provenance = registry.Provenance("com.test.a:Fire");
    ASSERT_EQ(provenance.size(), 2u);
    EXPECT_EQ(provenance[0].provider, "com.test.b");
    EXPECT_TRUE(provenance[0].isOverride);
    EXPECT_EQ(provenance[1].provider, "com.test.a");
    EXPECT_FALSE(provenance[1].isOverride);
    EXPECT_GT(provenance[0].rank, provenance[1].rank);
    EXPECT_TRUE(registry.Provenance("nobody.Provides").empty());
}

TEST_F(Registry, ChangeNotifications_FireWhenWinnerChanges)
{
    std::vector<std::string> changed;
    {
        const interi::sdk::Subscription subscription =
            registry.Subscribe([&](std::string_view id) { changed.emplace_back(id); });
        ASSERT_TRUE(registry.Provide("com.test.b", "service.Greeter", "b").has_value()); // new winner
        ASSERT_TRUE(registry.Provide("com.test.a", "service.Greeter", "a").has_value()); // shadowed: no change
        ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value()); // new winner
        registry.RemoveProvider("com.test.a"); // a's Fire goes (winner), Greeter keeps b
        EXPECT_EQ(registry.Resolve("com.test.a:Fire"), nullptr);
        EXPECT_EQ(*registry.Resolve("service.Greeter"), "b");
        registry.RemoveProvider("com.test.b");
        EXPECT_EQ(registry.Resolve("service.Greeter"), nullptr);
    }
    EXPECT_EQ(changed,
              (std::vector<std::string>{"service.Greeter", "com.test.a:Fire", "com.test.a:Fire", "service.Greeter"}));
    // Unsubscribed.
    ASSERT_TRUE(registry.Provide("com.test.b", "service.Greeter", "b").has_value());
    EXPECT_EQ(changed.size(), 4u);
}

TEST_F(Registry, ExtractAndRestore_RoundTrips)
{
    ASSERT_TRUE(registry.Provide("com.test.b", "service.Greeter", "b").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "service.Greeter", "a").has_value());
    ASSERT_TRUE(registry.Provide("com.test.a", "com.test.a:Fire", "a").has_value());
    ASSERT_TRUE(registry.Override("com.test.b", "com.test.a:Fire", "b").has_value());

    auto extracted = registry.ExtractProvider("com.test.b");
    EXPECT_EQ(extracted.size(), 2u);
    EXPECT_EQ(*registry.Resolve("service.Greeter"), "a");
    EXPECT_EQ(*registry.Resolve("com.test.a:Fire"), "a");

    registry.Restore(std::move(extracted));
    EXPECT_EQ(*registry.Resolve("service.Greeter"), "b");
    EXPECT_EQ(Providers(registry, "com.test.a:Fire"), (std::vector<std::string>{"com.test.b", "com.test.a"}));
}

TEST_F(Registry, UnknownProvider_Error)
{
    EXPECT_EQ(registry.Provide("com.test.nobody", "com.test.nobody:X", "x").error().code, ErrorCode::NotFound);
}

TEST(RegistryGraph, TestProject_PriorityOrder)
{
    const interi::sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    std::vector<std::string> ids;
    for (const auto& module : graph.modules)
    {
        ids.push_back(module.manifest.id);
    }
    EXPECT_EQ(ids, (std::vector<std::string>{"com.test.plugin", "com.test.b", "com.test.a", "com.test.ext"}));
}

namespace
{

/// A project with a physics-declaring module, an engine and an engine that extends it.
struct PhysicsProject
{
    interi::sdk::ModuleGraph graph;
    std::vector<std::string> contracts{"service.PhysicsQueries", "service.PhysicsBodies"};

    PhysicsProject()
    {
        const auto make = [](const char* id, interi::sdk::ModuleKind kind)
        {
            interi::sdk::ModuleManifest manifest;
            manifest.id = id;
            manifest.name = id;
            manifest.kind = kind;
            manifest.version = interi::sdk::ParseSemVer("1.0.0").value();
            return manifest;
        };
        auto game = make("com.test.game", interi::sdk::ModuleKind::Module);
        game.physicsEngines.push_back(
            {.id = "test.physics.alpha_plus", .version = {}, .isDefault = true, .library = {}});
        auto base = make("test.physics.alpha", interi::sdk::ModuleKind::PhysicsEngine);
        auto extender = make("test.physics.alpha_plus", interi::sdk::ModuleKind::PhysicsEngine);
        extender.extends = interi::sdk::PhysicsEngineRef{.id = "test.physics.alpha", .version = {}};
        const std::vector available = {game, base, extender};
        const std::vector<std::string> project = {"com.test.game"};
        graph = interi::sdk::ResolveModuleGraph(project, available, interi::sdk::CurrentHost()).value();
    }
};

} // namespace

TEST_F(Registry, PhysicsContract_FromNonEngine_Rejected)
{
    const PhysicsProject project;
    const ModuleSet moduleSet{project.graph, project.contracts};
    ContributionRegistry<std::string> contributions{moduleSet};
    auto fromModule = contributions.Provide("com.test.game", "service.PhysicsQueries", "game");
    ASSERT_FALSE(fromModule.has_value());
    EXPECT_EQ(fromModule.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(fromModule.error().message.find("Physics Engine"), std::string::npos);
    EXPECT_FALSE(contributions.Provide(kEngineProviderId, "service.PhysicsQueries", "engine").has_value());
    EXPECT_EQ(contributions.Resolve("service.PhysicsQueries"), nullptr);
}

TEST_F(Registry, PhysicsContract_FromEngine_Accepted)
{
    const PhysicsProject project;
    const ModuleSet moduleSet{project.graph, project.contracts};
    ContributionRegistry<std::string> contributions{moduleSet};
    ASSERT_TRUE(contributions.Provide("test.physics.alpha", "service.PhysicsQueries", "alpha").has_value());
    EXPECT_EQ(*contributions.Resolve("service.PhysicsQueries"), "alpha");
}

TEST_F(Registry, ExtenderEngine_OverridesBaseContract)
{
    const PhysicsProject project;
    const ModuleSet moduleSet{project.graph, project.contracts};
    ContributionRegistry<std::string> contributions{moduleSet};
    ASSERT_TRUE(contributions.Provide("test.physics.alpha", "service.PhysicsQueries", "alpha").has_value());
    ASSERT_TRUE(contributions.Provide("test.physics.alpha", "service.PhysicsBodies", "alpha").has_value());
    ASSERT_TRUE(contributions.Provide("test.physics.alpha_plus", "service.PhysicsQueries", "plus").has_value());
    // Per item: the extender replaces what it provides, the base's other items stay.
    EXPECT_EQ(*contributions.Resolve("service.PhysicsQueries"), "plus");
    EXPECT_EQ(*contributions.Resolve("service.PhysicsBodies"), "alpha");
    const auto provenance = contributions.Provenance("service.PhysicsQueries");
    ASSERT_EQ(provenance.size(), 2U);
    EXPECT_EQ(provenance[0].provider, "test.physics.alpha_plus");
    EXPECT_EQ(provenance[1].provider, "test.physics.alpha");
}
