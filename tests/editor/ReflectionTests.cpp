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
