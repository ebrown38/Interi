// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace interi;
using namespace interi::editor;

TEST(Reflection, Component_FieldsEnumerated)
{
    ReflectionRegistry registry;
    RegisterEngineReflection(registry);

    // The world Transform is derived from the hierarchy: only the local one is edited and saved.
    EXPECT_EQ(registry.Find(ComponentId<Transform>()), nullptr);
    const TypeReflection* transform = registry.Find(scene::LocalTransformComponent::kComponentName);
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->type, ComponentId<scene::LocalTransformComponent>());
    ASSERT_EQ(transform->fields.size(), 3u);
    EXPECT_EQ(transform->fields[0].name, "position");
    EXPECT_EQ(transform->fields[0].type, FieldType::Vec3);
    EXPECT_EQ(transform->fields[1].name, "rotation");
    EXPECT_EQ(transform->fields[1].type, FieldType::Quat);
    EXPECT_EQ(transform->fields[2].name, "scale");
    EXPECT_EQ(transform->fields[2].offset, offsetof(scene::LocalTransformComponent, scale));

    const TypeReflection* primitive = registry.Find(scene::PrimitiveComponent::kComponentName);
    ASSERT_NE(primitive, nullptr);
    EXPECT_EQ(primitive->fields[0].name, "shape");
    EXPECT_NE(registry.Find(scene::MeshComponent::kComponentName), nullptr);

    std::vector<std::string> names;
    for (const TypeReflection* type : registry.Types())
    {
        names.push_back(type->name);
    }
    EXPECT_TRUE(std::ranges::is_sorted(names));
    EXPECT_EQ(registry.Find("no.such.component"), nullptr);
}
