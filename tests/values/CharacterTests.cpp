// SPDX-License-Identifier: MIT
// Characters of the shared SDK test project (tests/sdk/modules): attr.Health is declared by com.test.ext (100,
// max 1000), com.test.a (80, max 200) and com.test.b (100, max 500); com.test.a defines Stamina, the Vitals set and
// the Soldier archetype (Health 120); com.test.b defines Armor and the Protection set and overrides Soldier
// (Vitals + Protection, Health 150, Armor 25).
#include <interi/ecs/World.h>
#include <interi/values/DataRegistry.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

using namespace interi;
using namespace interi::values;

static_assert(std::is_trivially_copyable_v<AttributeBlock>, "AttributeBlock survives module reloads by memcpy");

namespace
{

class Characters : public ::testing::Test
{
protected:
    void SetUp() override
    {
        for (const std::size_t index : graph.loadOrder)
        {
            Result<ModuleContent> content = LoadModuleContent(graph.modules[index].manifest);
            ASSERT_TRUE(content.has_value()) << content.error().message;
            ASSERT_TRUE(data.AddModule(*content).has_value());
        }
    }

    [[nodiscard]] AttributeLayout Soldier() const
    {
        Result<AttributeLayout> layout = data.Characters().Layout("com.test.a:Soldier");
        EXPECT_TRUE(layout.has_value()) << layout.error().message;
        return layout.value_or(AttributeLayout{});
    }

    sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    sdk::ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
    DataRegistry data{modules};
};

std::vector<std::string> Ids(const AttributeLayout& layout)
{
    std::vector<std::string> ids;
    for (const AttributeSlotInfo& slot : layout.slots)
    {
        ids.push_back(slot.id);
    }
    return ids;
}

} // namespace

TEST_F(Characters, Archetype_OverrideAndSharedAttributeWin)
{
    const AttributeLayout layout = Soldier();
    EXPECT_EQ(Ids(layout), (std::vector<std::string>{"attr.Health", "com.test.a:Stamina", "com.test.b:Armor"}));
    EXPECT_EQ(layout.slots[0].schema.defaultValue, Value(150.0f)); // B's archetype default
    EXPECT_EQ(layout.slots[0].schema.meta.max, 500.0);             // B's attr.Health definition
    EXPECT_EQ(layout.slots[2].schema.defaultValue, Value(std::int32_t{25}));

    std::vector<std::string> providers;
    for (const sdk::ProvenanceEntry& entry : data.Characters().AttributeProvenance("attr.Health"))
    {
        providers.push_back(entry.provider);
    }
    EXPECT_EQ(providers, (std::vector<std::string>{"com.test.b", "com.test.a", "com.test.ext"}));
}

TEST_F(Characters, Block_DefaultsAndPerEntityOverrides)
{
    const AttributeLayout layout = Soldier();
    const std::vector<std::pair<std::string, Value>> overrides = {{"attr.Health", 300.0f}};
    Result<AttributeBlock> block = MakeAttributeBlock(layout, overrides);
    ASSERT_TRUE(block.has_value()) << block.error().message;
    EXPECT_EQ(GetAttribute<float>(*block, "attr.Health"), 300.0f);
    EXPECT_EQ(GetAttribute<float>(*block, "com.test.a:Stamina"), 50.0f);
    EXPECT_EQ(GetAttribute<std::int32_t>(*block, "com.test.b:Armor"), 25);
    EXPECT_EQ(GetAttribute<float>(*block, "com.test.b:Armor").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(GetAttribute<float>(*block, "attr.Mana").error().code, ErrorCode::NotFound);

    ASSERT_TRUE(SetAttribute(*block, layout, "com.test.b:Armor", std::int32_t{40}).has_value());
    EXPECT_EQ(GetAttribute<std::int32_t>(*block, "com.test.b:Armor"), 40);
    EXPECT_FALSE(SetAttribute(*block, layout, "com.test.b:Armor", std::int32_t{101}).has_value());

    const std::vector<std::pair<std::string, Value>> tooHigh = {{"attr.Health", 600.0f}};
    EXPECT_FALSE(MakeAttributeBlock(layout, tooHigh).has_value());
    const std::vector<std::pair<std::string, Value>> unknown = {{"attr.Mana", 1.0f}};
    EXPECT_EQ(MakeAttributeBlock(layout, unknown).error().code, ErrorCode::NotFound);
}

TEST_F(Characters, ProjectDefault_BeatsArchetypeDefault)
{
    ASSERT_TRUE(data.Characters().SetProjectDefault("com.test.a:Soldier", "attr.Health", 175.0f).has_value());
    EXPECT_EQ(Soldier().slots[0].schema.defaultValue, Value(175.0f));
    EXPECT_FALSE(data.Characters().SetProjectDefault("com.test.a:Soldier", "attr.Health", 9999.0f).has_value());
    EXPECT_FALSE(data.Characters().SetProjectDefault("com.test.a:Soldier", "attr.Mana", 1.0f).has_value());
    EXPECT_FALSE(data.Characters().SetProjectDefault("com.test.a:Nobody", "attr.Health", 1.0f).has_value());
    EXPECT_TRUE(data.Characters().ClearProjectDefault("com.test.a:Soldier", "attr.Health"));
    EXPECT_EQ(Soldier().slots[0].schema.defaultValue, Value(150.0f));
}

TEST_F(Characters, ModuleRemoved_BlocksMigrateByAttributeKey)
{
    World world;
    const Entity entity = world.CreateEntity();
    const std::vector<std::pair<std::string, Value>> overrides = {{"attr.Health", 300.0f},
                                                                  {"com.test.a:Stamina", 20.0f}};
    (void)world.Add<AttributeBlock>(entity, MakeAttributeBlock(Soldier(), overrides).value());
    EXPECT_EQ(data.Characters().MigrateBlocks(world), 0u) << "already current";

    // Without B, Soldier is A's: Vitals only, attr.Health is A's (max 200).
    ExtractedData b = data.ExtractProvider("com.test.b");
    EXPECT_EQ(data.Characters().MigrateBlocks(world), 1u);
    const AttributeBlock& migrated = *world.Get<AttributeBlock>(entity);
    EXPECT_EQ(migrated.count, 2u);
    EXPECT_EQ(GetAttribute<float>(migrated, "attr.Health"), 200.0f) << "clamped to the new range";
    EXPECT_EQ(GetAttribute<float>(migrated, "com.test.a:Stamina"), 20.0f) << "kept";
    EXPECT_FALSE(GetAttribute(migrated, "com.test.b:Armor").has_value()) << "dropped";

    data.Restore(std::move(b));
    EXPECT_EQ(data.Characters().MigrateBlocks(world), 1u);
    EXPECT_EQ(GetAttribute<std::int32_t>(*world.Get<AttributeBlock>(entity), "com.test.b:Armor"), 25) << "new: default";
}

TEST(AttributeBlock, Migrate_ConvertsTypes)
{
    const auto slot = [](std::string id, ValueType type, Value value)
    {
        AttributeSlotInfo info;
        info.id = std::move(id);
        info.key = AttributeKey(info.id);
        info.schema.type = type;
        info.schema.defaultValue = std::move(value);
        return info;
    };
    AttributeLayout before;
    before.archetype = "x:A";
    before.archetypeKey = AttributeKey(before.archetype);
    before.version = 1;
    before.slots = {slot("x:Count", ValueType::Int, std::int32_t{7}), slot("x:Speed", ValueType::Float, 2.75f),
                    slot("x:Alive", ValueType::Bool, true)};
    AttributeBlock block = MakeAttributeBlock(before).value();

    AttributeLayout after = before;
    after.version = 2;
    after.slots = {slot("x:Count", ValueType::Float, 0.0f), slot("x:Speed", ValueType::Int, std::int32_t{0}),
                   slot("x:Alive", ValueType::Int, std::int32_t{0})};
    after.slots[1].schema.meta.max = 2.0;
    EXPECT_TRUE(MigrateAttributeBlock(block, after));
    EXPECT_EQ(GetAttribute<float>(block, "x:Count"), 7.0f);
    EXPECT_EQ(GetAttribute<std::int32_t>(block, "x:Speed"), 2);
    EXPECT_EQ(GetAttribute<std::int32_t>(block, "x:Alive"), 1);
    EXPECT_FALSE(MigrateAttributeBlock(block, after));
}

TEST(CharacterContent, Errors)
{
    const auto parse = [](const std::string& sections)
    { return ParseModuleContent(R"({"id": "com.x.y", )" + sections + "}", "com.x.y"); };
    EXPECT_FALSE(parse(R"("attributes": [{"id": "Name", "type": "string"}])").has_value());
    EXPECT_FALSE(parse(R"("attributeSets": [{"id": "S"}])").has_value());
    EXPECT_FALSE(parse(R"("attributeSets": [{"id": "S", "attributes": ["a.b", "a.b"]}])").has_value());
    EXPECT_FALSE(parse(R"("archetypes": [{"id": "A", "sets": [], "defaults": []}])").has_value());

    sdk::ModuleSet modules;
    modules.AddProvider(
        sdk::Provider{.id = "com.x.y", .kind = sdk::ModuleKind::Module, .version = {}, .rank = 1, .overrides = {}});
    CharacterRegistry registry(modules);
    const Result<ModuleContent> content = parse(R"(
        "attributes": [{"id": "Hp", "type": "float", "default": 1}],
        "attributeSets": [{"id": "S", "attributes": ["com.x.y:Hp"]}],
        "archetypes": [{"id": "Bad", "sets": ["com.x.y:S"], "defaults": {"com.x.y:Mana": 1}},
                       {"id": "Wrong", "sets": ["com.x.y:S"], "defaults": {"com.x.y:Hp": "lots"}},
                       {"id": "Missing", "sets": ["com.x.y:Nope"]}])");
    ASSERT_TRUE(content.has_value()) << content.error().message;
    ASSERT_TRUE(registry.AddModule(*content).has_value());
    EXPECT_EQ(registry.Layout("com.x.y:Bad").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.Layout("com.x.y:Wrong").error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(registry.Layout("com.x.y:Missing").error().code, ErrorCode::NotFound);
    EXPECT_EQ(registry.Layout("com.x.y:Nobody").error().code, ErrorCode::NotFound);
}
