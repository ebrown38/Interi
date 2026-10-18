// SPDX-License-Identifier: MIT
#pragma once

// A small project for chain tests: module com.test.a (rank 1) with Values and actions, Plugin com.test.plugin
// (rank 2) that may override com.test.a:Shoot, and the core library.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/CoreLibrary.h>
#include <interi/chain/Event.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ValueRegistry.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::chain::testing
{

inline EventParam Param(std::string name, values::ValueType type, values::Value defaultValue)
{
    EventParam param;
    param.name = std::move(name);
    param.schema.type = type;
    param.schema.defaultValue = std::move(defaultValue);
    return param;
}

inline EventDefinition Action(std::vector<EventParam> inputs, std::vector<EventOutput> outputs, EventImpl impl)
{
    EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.implementation = std::make_shared<const EventImpl>(std::move(impl));
    return definition;
}

/// com.test.a:Shoot(range: float = 10) -> Hit {damage: float, target: entity}, Miss.
/// Hits (damage = range * 2, target = the activation's entity) when range > 0, else misses.
inline EventDefinition ShootEvent(float factor = 2.0f)
{
    using values::ValueType;
    return Action(
        {Param("range", ValueType::Float, 10.0f)},
        {{.name = "Hit",
          .payload = {{.name = "damage", .type = ValueType::Float}, {.name = "target", .type = ValueType::EntityRef}}},
         {.name = "Miss", .payload = {}}},
        [factor](EventCall& call)
        {
            const float range = call.Get<float>(0);
            if (range > 0.0f)
            {
                call.Fire(0, {range * factor, values::EntityRef{.id = call.Entity()}});
            }
            else
            {
                call.Fire(1);
            }
        });
}

struct TestProject
{
    sdk::ModuleSet modules;
    std::unique_ptr<values::ValueRegistry> values;
    std::unique_ptr<ChainRegistry> chains;

    TestProject()
    {
        sdk::Provider a;
        a.id = "com.test.a";
        a.rank = 1;
        modules.AddProvider(a);
        sdk::Provider plugin;
        plugin.id = "com.test.plugin";
        plugin.kind = sdk::ModuleKind::Plugin;
        plugin.rank = 2;
        plugin.overrides = {{.target = "com.test.a:Shoot", .version = {}}};
        modules.AddProvider(plugin);
        for (std::string& id : CoreContracts())
        {
            modules.DeclareContract(std::move(id));
        }
        values = std::make_unique<values::ValueRegistry>(modules);
        chains = std::make_unique<ChainRegistry>(modules);
        EXPECT_TRUE(RegisterCoreLibrary(*chains).has_value());
        AddValue("com.test.a:Health", values::ValueType::Float, 100.0f);
        AddValue("com.test.a:Score", values::ValueType::Int, std::int32_t{0});
        AddValue("com.test.a:Armed", values::ValueType::Bool, true);
        EXPECT_TRUE(chains->Events().Provide("com.test.a", "com.test.a:Shoot", ShootEvent()).has_value());
    }

    void AddValue(std::string id, values::ValueType type, values::Value defaultValue)
    {
        values::ValueDefinition definition;
        definition.id = std::move(id);
        definition.schema.type = type;
        definition.schema.defaultValue = std::move(defaultValue);
        EXPECT_TRUE(values->Add("com.test.a", definition).has_value());
    }
};

/// Parses chain JSON, failing the test on error.
inline ChainAsset Chain(std::string_view json)
{
    auto asset = ParseChain(json);
    EXPECT_TRUE(asset.has_value()) << (asset ? "" : asset.error().message);
    return asset ? std::move(*asset) : ChainAsset{};
}

} // namespace interi::chain::testing
