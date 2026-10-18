// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/chain/Event.h>
#include <interi/sdk/ContributionRegistry.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace interi;
using namespace interi::chain;
using values::ValueType;

namespace
{

EventDefinition Action(std::vector<EventParam> inputs, std::vector<EventOutput> outputs)
{
    EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.implementation = std::make_shared<const EventImpl>([](EventCall& call) { call.Fire(0); });
    return definition;
}

EventParam Param(std::string name, ValueType type)
{
    EventParam param;
    param.name = std::move(name);
    param.schema.type = type;
    param.schema.defaultValue = values::ZeroValue(type);
    return param;
}

/// Module com.test.a (rank 1) and Plugin com.test.plugin (rank 2) that may override com.test.a:FireGun.
struct Modules
{
    sdk::ModuleSet set;

    Modules()
    {
        sdk::Provider a;
        a.id = "com.test.a";
        a.rank = 1;
        set.AddProvider(a);
        sdk::Provider plugin;
        plugin.id = "com.test.plugin";
        plugin.kind = sdk::ModuleKind::Plugin;
        plugin.rank = 2;
        plugin.overrides = {{.target = "com.test.a:FireGun", .version = {}}};
        set.AddProvider(plugin);
        for (std::string& id : CoreContracts())
        {
            set.DeclareContract(std::move(id));
        }
    }
};

} // namespace

TEST(Event, CoreLibrary_RegistersTriggers)
{
    Modules modules;
    ChainRegistry registry(modules.set);
    ASSERT_TRUE(RegisterCoreLibrary(registry).has_value());
    const EventDefinition* tick = registry.Events().Resolve(kTriggerTick);
    ASSERT_NE(tick, nullptr);
    EXPECT_EQ(tick->kind, EventKind::Trigger);
    ASSERT_EQ(tick->outputs.size(), 1u);
    EXPECT_EQ(tick->outputs[0].payload.size(), 2u);
    const EventDefinition* input = registry.Events().Resolve(kTriggerInputAction);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(input->InputIndex(kTriggerKeyInput), 0u);
}

TEST(Event, OverrideWithOtherSignature_Rejected)
{
    Modules modules;
    EventRegistry events(modules.set);
    const std::vector<EventOutput> outputs{{.name = "Hit", .payload = {{.name = "damage", .type = ValueType::Float}}}};
    ASSERT_TRUE(events.Provide("com.test.a", "com.test.a:FireGun", Action({Param("range", ValueType::Float)}, outputs))
                    .has_value());

    auto otherInput =
        events.Override("com.test.plugin", "com.test.a:FireGun", Action({Param("range", ValueType::Int)}, outputs));
    EXPECT_FALSE(otherInput.has_value());
    auto otherPayload = events.Override("com.test.plugin", "com.test.a:FireGun",
                                        Action({Param("range", ValueType::Float)}, {{.name = "Hit", .payload = {}}}));
    EXPECT_FALSE(otherPayload.has_value());

    EventDefinition same = Action({Param("range", ValueType::Float)}, outputs);
    same.tooltip = "faster";
    ASSERT_TRUE(events.Override("com.test.plugin", "com.test.a:FireGun", std::move(same)).has_value());
    EXPECT_EQ(events.Resolve("com.test.a:FireGun")->tooltip, "faster");
    ASSERT_EQ(events.Registry().Provenance("com.test.a:FireGun").size(), 2u);
}

TEST(Event, InvalidDefinitions_Rejected)
{
    Modules modules;
    EventRegistry events(modules.set);
    EventDefinition noImpl = Action({}, {});
    noImpl.implementation = nullptr;
    EXPECT_FALSE(events.Provide("com.test.a", "com.test.a:A", noImpl).has_value());

    EXPECT_FALSE(
        events
            .Provide("com.test.a", "com.test.a:B", Action({Param("x", ValueType::Int), Param("x", ValueType::Int)}, {}))
            .has_value());
    EXPECT_FALSE(
        events.Provide("com.test.a", "com.test.a:C", Action({Param("bad name", ValueType::Int)}, {})).has_value());

    EventParam typed;
    typed.name = "value";
    typed.typeFrom = "id";
    EXPECT_FALSE(events.Provide("com.test.a", "com.test.a:D", Action({typed}, {})).has_value());
    EXPECT_TRUE(
        events.Provide("com.test.a", "com.test.a:E", Action({Param("id", ValueType::String), typed}, {})).has_value());

    EventDefinition trigger = Action({}, {{.name = "Fired", .payload = {}}});
    trigger.kind = EventKind::Trigger;
    EXPECT_FALSE(events.Provide("com.test.a", "com.test.a:F", trigger).has_value());
    trigger.implementation = nullptr;
    EXPECT_TRUE(events.Provide("com.test.a", "com.test.a:F", trigger).has_value());
}

TEST(Event, ExtractAndRestore_RoundTrips)
{
    Modules modules;
    ChainRegistry registry(modules.set);
    ASSERT_TRUE(registry.Events().Provide("com.test.a", "com.test.a:FireGun", Action({}, {})).has_value());
    int notifications = 0;
    auto subscriptions = registry.Subscribe([&](std::string_view) { ++notifications; });
    auto extracted = registry.ExtractProvider("com.test.a");
    EXPECT_EQ(registry.Events().Resolve("com.test.a:FireGun"), nullptr);
    registry.Restore(std::move(extracted));
    EXPECT_NE(registry.Events().Resolve("com.test.a:FireGun"), nullptr);
    EXPECT_EQ(notifications, 2);
}
