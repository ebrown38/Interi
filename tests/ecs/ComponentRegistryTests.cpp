// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

struct NamedA
{
    static constexpr const char* kComponentName = "test.Shared";
    float value = 0.0f;
};

// A different C++ type with the same explicit name and layout: one component type in the registry (this is how a
// hot-reloaded module's copy of a type finds the host's id).
struct NamedB
{
    static constexpr const char* kComponentName = "test.Shared";
    float value = 0.0f;
};

struct NamedV2
{
    static constexpr const char* kComponentName = "test.Shared";
    static constexpr std::uint32_t kComponentVersion = 2;
    float value = 0.0f;
};

struct Trivial
{
    int x = 0;
};

struct NonTrivial
{
    std::string text;
};

interi::ComponentInfo InfoNamed(std::string name)
{
    interi::ComponentInfo info;
    info.name = std::move(name);
    info.size = 4;
    info.alignment = 4;
    return info;
}

} // namespace

TEST(ComponentRegistry, ExplicitName_UsedAsName)
{
    EXPECT_EQ(interi::ComponentInfoOf(interi::ComponentId<NamedA>()).name, "test.Shared");
}

TEST(ComponentRegistry, SameExplicitName_SameLayout_SameId)
{
    EXPECT_EQ(interi::ComponentId<NamedA>(), interi::ComponentId<NamedB>());
}

TEST(ComponentRegistry, SameName_LayoutChanged_NewId)
{
    const interi::ComponentTypeId v1 = interi::ComponentId<NamedA>();
    const interi::ComponentTypeId v2 = interi::ComponentId<NamedV2>();
    EXPECT_NE(v1, v2);
    EXPECT_EQ(interi::ComponentInfoOf(v2).name, "test.Shared");
    EXPECT_EQ(interi::ComponentInfoOf(v2).version, 2u);
    EXPECT_EQ(interi::ComponentInfoOf(v1).version, 1u);
}

TEST(ComponentRegistry, AnonymousNamespace_NotDeduped)
{
    const interi::ComponentTypeId clangA = interi::RegisterComponent(InfoNamed("(anonymous namespace)::Dup"));
    const interi::ComponentTypeId clangB = interi::RegisterComponent(InfoNamed("(anonymous namespace)::Dup"));
    EXPECT_NE(clangA, clangB);
    const interi::ComponentTypeId msvcA = interi::RegisterComponent(InfoNamed("struct `anonymous namespace'::Dup"));
    const interi::ComponentTypeId msvcB = interi::RegisterComponent(InfoNamed("struct `anonymous namespace'::Dup"));
    EXPECT_NE(msvcA, msvcB);
    const interi::ComponentTypeId namedA = interi::RegisterComponent(InfoNamed("test.RegisteredTwice"));
    const interi::ComponentTypeId namedB = interi::RegisterComponent(InfoNamed("test.RegisteredTwice"));
    EXPECT_EQ(namedA, namedB);
    EXPECT_GE(interi::ComponentTypeCount(), namedA + 1);
}

TEST(ComponentRegistry, TrivialComponent_NoFunctionPointers)
{
    const interi::ComponentInfo& trivial = interi::ComponentInfoOf(interi::ComponentId<Trivial>());
    EXPECT_EQ(trivial.relocate, nullptr);
    EXPECT_EQ(trivial.moveAssign, nullptr);
    EXPECT_EQ(trivial.destroy, nullptr);
    const interi::ComponentInfo& nonTrivial = interi::ComponentInfoOf(interi::ComponentId<NonTrivial>());
    EXPECT_NE(nonTrivial.relocate, nullptr);
    EXPECT_NE(nonTrivial.moveAssign, nullptr);
    EXPECT_NE(nonTrivial.destroy, nullptr);
}

TEST(ComponentRegistry, MixedTrivialAndNonTrivial_SurviveStructuralMoves)
{
    interi::World world;
    std::vector<interi::Entity> entities;
    for (int i = 0; i < 300; ++i)
    {
        entities.push_back(
            world.CreateEntity(Trivial{i}, NonTrivial{std::string(40, static_cast<char>('a' + i % 26))}));
    }
    for (int i = 0; i < 300; i += 3)
    {
        ASSERT_TRUE(world.DestroyEntity(entities[static_cast<std::size_t>(i)]));
    }
    for (int i = 1; i < 300; i += 3)
    {
        ASSERT_TRUE(world.Remove<NonTrivial>(entities[static_cast<std::size_t>(i)]));
    }
    for (int i = 0; i < 300; ++i)
    {
        const interi::Entity entity = entities[static_cast<std::size_t>(i)];
        if (i % 3 == 0)
        {
            EXPECT_FALSE(world.IsAlive(entity));
            continue;
        }
        ASSERT_NE(world.Get<Trivial>(entity), nullptr);
        EXPECT_EQ(world.Get<Trivial>(entity)->x, i);
        if (i % 3 == 2)
        {
            EXPECT_EQ(world.Get<NonTrivial>(entity)->text, std::string(40, static_cast<char>('a' + i % 26)));
        }
    }
}

TEST(ComponentRegistry, GetErased_PointsAtComponent)
{
    interi::World world;
    const interi::Entity entity = world.CreateEntity(Trivial{7});
    const void* erased = world.GetErased(entity, interi::ComponentId<Trivial>());
    ASSERT_NE(erased, nullptr);
    EXPECT_EQ(erased, world.Get<Trivial>(entity));
    EXPECT_EQ(world.GetErased(entity, interi::ComponentId<NonTrivial>()), nullptr);
}
