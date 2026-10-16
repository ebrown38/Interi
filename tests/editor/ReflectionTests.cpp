// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/gameplay/Vehicle.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace interi;
using namespace interi::editor;

namespace
{

struct Probe
{
    static constexpr const char* kComponentName = "interi.tests.editor.Probe";
    bool flag = false;
    std::uint32_t count = 0;
    float value = 0.0f;
    Vec3 position;
    Quat rotation;
    std::string label;
};

} // namespace

TEST(Reflection, Component_FieldsEnumerated)
{
    ReflectionRegistry registry;
    RegisterEngineReflection(registry);

    const TypeReflection* transform = registry.Find(ComponentInfoOf(ComponentId<Transform>()).name);
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->type, ComponentId<Transform>());
    ASSERT_EQ(transform->fields.size(), 3u);
    EXPECT_EQ(transform->fields[0].name, "position");
    EXPECT_EQ(transform->fields[0].type, FieldType::Vec3);
    EXPECT_EQ(transform->fields[1].name, "rotation");
    EXPECT_EQ(transform->fields[1].type, FieldType::Quat);
    EXPECT_EQ(transform->fields[2].name, "scale");
    EXPECT_EQ(transform->fields[2].offset, offsetof(Transform, scale));

    const TypeReflection* box = registry.Find("interi.editor.StaticBox");
    ASSERT_NE(box, nullptr);
    ASSERT_EQ(box->fields.size(), 1u);
    EXPECT_EQ(box->fields[0].name, "halfExtents");

    const TypeReflection* vehicle = registry.Find(gameplay::VehicleComponent::kComponentName);
    ASSERT_NE(vehicle, nullptr);
    EXPECT_EQ(vehicle->fields[0].name, "health");
    EXPECT_EQ(registry.Find(ComponentId<gameplay::VehicleComponent>()), vehicle);

    std::vector<std::string> names;
    for (const TypeReflection* type : registry.Types())
    {
        names.push_back(type->name);
    }
    EXPECT_TRUE(std::ranges::is_sorted(names));
    EXPECT_EQ(registry.Find("no.such.component"), nullptr);
}

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
    });
    ASSERT_EQ(probe.fields.size(), 6u);
    EXPECT_EQ(probe.fields[5].type, FieldType::String);

    Probe object;
    const std::vector<FieldValue> values = {
        true, 7u, 2.5f, Vec3(1, 2, 3), Quat{0.0f, 1.0f, 0.0f, 0.0f}, std::string("hello")};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        EXPECT_TRUE(WriteField(probe.fields[i], &object, values[i])) << i;
        EXPECT_EQ(ReadField(probe.fields[i], &object), values[i]) << i;
    }
    EXPECT_TRUE(object.flag);
    EXPECT_EQ(object.count, 7u);
    EXPECT_EQ(object.label, "hello");
}

TEST(Reflection, WrongType_NotWritten)
{
    ReflectionRegistry registry;
    const TypeReflection& probe = registry.Register<Probe>({Field("value", &Probe::value)});
    Probe object;
    object.value = 1.0f;
    EXPECT_FALSE(WriteField(probe.fields[0], &object, std::uint32_t{3}));
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
}
