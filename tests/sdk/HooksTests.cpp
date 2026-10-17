// SPDX-License-Identifier: MIT
#include <interi/sdk/Hooks.h>

#include "SdkTestModules.h"
#include <gtest/gtest.h>

#include <memory>
#include <string>

using interi::ErrorCode;
using interi::sdk::ClassFactory;
using interi::sdk::FactorySignature;
using interi::sdk::HookNext;
using interi::sdk::HookPoint;
using interi::sdk::HookRegistry;
using interi::sdk::ModuleScope;
using interi::sdk::ModuleSet;

namespace
{

using FireHook = int(int);

int DoubleFire(const HookNext<FireHook>& next, int damage)
{
    return 2 * next(damage);
}

struct Weapon
{
    Weapon() = default;
    Weapon(const Weapon&) = delete;
    Weapon& operator=(const Weapon&) = delete;
    Weapon(Weapon&&) = delete;
    Weapon& operator=(Weapon&&) = delete;
    virtual ~Weapon() = default;
    [[nodiscard]] virtual std::string Name() const { return "pistol"; }
};

// A Plugin's decorator around whatever the lower factory makes.
struct Silenced final : Weapon
{
    explicit Silenced(std::unique_ptr<Weapon> inner)
        : m_inner(std::move(inner))
    {
    }
    [[nodiscard]] std::string Name() const override { return "silenced " + m_inner->Name(); }
    std::unique_ptr<Weapon> m_inner;
};

class HookFixture : public ::testing::Test
{
protected:
    interi::sdk::ModuleGraph graph = interi::testing::ResolveSdkTestProject();
    ModuleSet modules{graph, interi::testing::kSdkTestEngineContracts};
    HookRegistry hooks{modules};
    ModuleScope a{hooks, "com.test.a"};
    ModuleScope b{hooks, "com.test.b"};
    ModuleScope plugin{hooks, "com.test.plugin"};
};

class Hook : public HookFixture
{
};
class Override : public HookFixture
{
};

} // namespace

TEST_F(Hook, Declared_CallsDefault)
{
    ASSERT_TRUE(a.DeclareHook<FireHook>(a.Local("Fire"),
                                        [](const HookNext<FireHook>& next, int damage)
                                        {
                                            EXPECT_FALSE(next.HasNext());
                                            return damage + 1;
                                        })
                    .has_value());
    const HookPoint<FireHook> fire = hooks.Find<FireHook>("com.test.a:Fire");
    ASSERT_TRUE(fire.IsValid());
    EXPECT_EQ(fire(10), 11);
}

TEST_F(Override, CallsNext_ChainsToLowerImpl)
{
    ASSERT_TRUE(
        a.DeclareHook<FireHook>(a.Local("Fire"), [](const HookNext<FireHook>&, int damage) { return damage + 1; })
            .has_value());
    const HookPoint<FireHook> fire = hooks.Find<FireHook>("com.test.a:Fire"); // resolved before the overrides
    ASSERT_TRUE(INTERI_OVERRIDE(b, "com.test.a:Fire", &DoubleFire).has_value());
    EXPECT_EQ(fire(10), 22); // B doubles A's 11

    // The Plugin (highest) wraps B, which still wraps A.
    ASSERT_TRUE(INTERI_OVERRIDE(plugin, "com.test.a:Fire",
                                [](const HookNext<FireHook>& next, int damage) { return next(damage) - 100; })
                    .has_value());
    EXPECT_EQ(fire(10), -78);

    // Unloading B takes its link out of the chain.
    hooks.Registry().RemoveProvider("com.test.b");
    EXPECT_EQ(fire(10), -89);
}

TEST_F(Override, UnknownTarget_Error)
{
    auto result = INTERI_OVERRIDE(b, "com.test.a:Fire", &DoubleFire);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(hooks.Find<FireHook>("com.test.a:Fire").IsValid());
}

TEST_F(Override, SignatureMismatch_Error)
{
    ASSERT_TRUE(a.DeclareHook<FireHook>(a.Local("Fire"), [](const HookNext<FireHook>&, int damage) { return damage; })
                    .has_value());
    auto result =
        INTERI_OVERRIDE(b, "com.test.a:Fire", [](const HookNext<float(float)>& next, float x) { return next(x); });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(hooks.Find<float(float)>("com.test.a:Fire").IsValid());
    EXPECT_TRUE(hooks.Find<FireHook>("com.test.a:Fire").IsValid());
}

TEST_F(Override, UndeclaredInManifest_Error)
{
    ASSERT_TRUE(
        a.DeclareHook<FireHook>(a.Local("Jump"), [](const HookNext<FireHook>&, int x) { return x; }).has_value());
    EXPECT_EQ(INTERI_OVERRIDE(b, "com.test.a:Jump", &DoubleFire).error().code, ErrorCode::InvalidArgument);
}

TEST_F(Hook, Shared_HigherProviderWinsAndChains)
{
    ModuleScope ext{hooks, "com.test.ext"};
    using Greet = std::string(const std::string&);
    ASSERT_TRUE(ext.DeclareHook<Greet>("service.Greeter",
                                       [](const HookNext<Greet>&, const std::string& name) { return "hello " + name; })
                    .has_value());
    ASSERT_TRUE(b.DeclareHook<Greet>("service.Greeter", [](const HookNext<Greet>& next, const std::string& name)
                                     { return next(name) + "!"; })
                    .has_value());
    EXPECT_EQ(hooks.Find<Greet>("service.Greeter")("bob"), "hello bob!");
}

TEST_F(Hook, ClassFactory_OverrideWrapsLowerFactory)
{
    ASSERT_TRUE(b.DeclareFactory<Weapon>(b.Local("Reload")).has_value());
    const ClassFactory<Weapon> factory = hooks.Find<FactorySignature<Weapon>>("com.test.b:Reload");
    ASSERT_TRUE(factory.IsValid());
    EXPECT_EQ(factory()->Name(), "pistol");

    ASSERT_TRUE(INTERI_OVERRIDE(plugin, "com.test.b:Reload",
                                [](const HookNext<FactorySignature<Weapon>>& next) -> std::unique_ptr<Weapon>
                                { return std::make_unique<Silenced>(next()); })
                    .has_value());
    EXPECT_EQ(factory()->Name(), "silenced pistol");
}
