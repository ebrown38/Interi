// SPDX-License-Identifier: MIT
#include <interi/ecs/World.h>
#include <interi/values/Reflection.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace interi;
using namespace interi::values;

namespace
{

struct Probe
{
    static constexpr const char* kComponentName = "interi.tests.values.Probe";
    bool flag = false;
    std::uint32_t count = 0;
    float value = 0.0f;
    Vec3 position;
    Quat rotation;
    std::string label;
    std::int32_t signedCount = 0;
    Color tint;
    EnumValue mode;
    AssetRef mesh;
    EntityRef target;
    ValueList waypoints;
};

} // namespace

TEST(Reflection, ReadWrite_EveryFieldType)
{
    ReflectionRegistry registry;
    const TypeReflection& probe = registry.Register<Probe>({
        Field("flag", &Probe::flag),
        Field("count", &Probe::count),
        Field("value", &Probe::value),
        Field("position", &Probe::position),
        Field("rotation", &Probe::rotation),
        Field("label", &Probe::label),
        Field("signedCount", &Probe::signedCount),
        Field("tint", &Probe::tint),
        Field("mode", &Probe::mode),
        Field("mesh", &Probe::mesh),
        Field("target", &Probe::target),
        Field("waypoints", &Probe::waypoints),
    });
    ASSERT_EQ(probe.fields.size(), kValueTypeCount);
    for (std::size_t i = 0; i < probe.fields.size(); ++i)
    {
        EXPECT_EQ(probe.fields[i].type, static_cast<ValueType>(i)) << probe.fields[i].name;
    }

    Probe object;
    const std::vector<Value> values = {
        true,
        7u,
        2.5f,
        Vec3(1, 2, 3),
        Quat{0.0f, 1.0f, 0.0f, 0.0f},
        std::string("hello"),
        std::int32_t{-4},
        Color{.r = 0.5f, .g = 0.25f, .b = 1.0f, .a = 0.75f},
        EnumValue{.name = "Fast"},
        AssetRef{.path = "meshes/crate.gltf"},
        EntityRef{.id = 42},
        ValueList{.element = ValueType::Vec3, .items = {Vec3(1, 0, 0), Vec3(0, 0, 1)}},
    };
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        EXPECT_TRUE(WriteField(probe.fields[i], &object, values[i])) << i;
        EXPECT_EQ(ReadField(probe.fields[i], &object), values[i]) << i;
    }
    EXPECT_TRUE(object.flag);
    EXPECT_EQ(object.count, 7u);
    EXPECT_EQ(object.label, "hello");
    EXPECT_EQ(object.signedCount, -4);
    EXPECT_EQ(object.mode.name, "Fast");
    EXPECT_EQ(object.target.id, 42u);
    EXPECT_EQ(object.waypoints.items.size(), 2u);
}

TEST(Reflection, FieldMeta_Kept)
{
    ValueMeta meta;
    meta.min = 0.0;
    meta.max = 10.0;
    meta.unit = "m";
    const FieldInfo field = Field("value", &Probe::value, meta);
    EXPECT_EQ(field.meta.min, 0.0);
    EXPECT_EQ(field.meta.max, 10.0);
    EXPECT_EQ(field.meta.unit, "m");
}

TEST(Reflection, WrongType_NotWritten)
{
    ReflectionRegistry registry;
    const TypeReflection& probe = registry.Register<Probe>({Field("value", &Probe::value)});
    Probe object;
    object.value = 1.0f;
    EXPECT_FALSE(WriteField(probe.fields[0], &object, std::uint32_t{3}));
    EXPECT_FALSE(WriteField(probe.fields[0], &object, std::int32_t{3}));
    EXPECT_EQ(object.value, 1.0f);
}

TEST(Reflection, AddDefault_AddsComponent)
{
    ReflectionRegistry registry;
    const TypeReflection& probe = registry.Register<Probe>({Field("count", &Probe::count)});
    World world;
    const Entity entity = world.CreateEntity();
    probe.addDefault(world, entity);
    EXPECT_TRUE(world.Has<Probe>(entity));
    EXPECT_EQ(registry.Find(ComponentId<Probe>()), &probe);
    EXPECT_EQ(registry.Find("interi.tests.values.Probe"), &probe);
}
