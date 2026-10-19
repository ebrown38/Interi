// SPDX-License-Identifier: MIT
#include <interi/sdk/Hooks.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/sdk/PhysicsQueries.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using interi::Vec3;
using interi::sdk::HookNext;
using interi::sdk::HookRegistry;
using interi::sdk::IPhysicsQueries;
using interi::sdk::ModuleKind;
using interi::sdk::ModuleScope;
using interi::sdk::ModuleSet;
using interi::sdk::PhysicsHit;
using interi::sdk::PhysicsLayerMask;
using interi::sdk::PhysicsQueriesService;

namespace
{

/// A fake engine: a floor at y = 0 for rays, and one overlap per layer bit.
class FloorQueries final : public IPhysicsQueries
{
public:
    [[nodiscard]] std::optional<PhysicsHit> Raycast(const Vec3& origin, const Vec3& displacement,
                                                    PhysicsLayerMask layers) const override
    {
        if (layers == 0 || origin.y <= 0.0f || origin.y + displacement.y > 0.0f)
        {
            return std::nullopt;
        }
        const float fraction = origin.y / -displacement.y;
        return PhysicsHit{.object = 7,
                          .point = origin + displacement * fraction,
                          .normal = Vec3(0.0f, 1.0f, 0.0f),
                          .fraction = fraction};
    }
    [[nodiscard]] std::optional<PhysicsHit> SphereCast(const Vec3& start, float radius, const Vec3& displacement,
                                                       PhysicsLayerMask layers) const override
    {
        return Raycast(start - Vec3(0.0f, radius, 0.0f), displacement, layers);
    }
    void OverlapSphere(const Vec3&, float, std::vector<PhysicsHit>& out, PhysicsLayerMask layers) const override
    {
        out.clear();
        for (std::uint64_t bit = 0; bit < 32; ++bit)
        {
            if (((layers >> bit) & 1U) != 0U)
            {
                PhysicsHit hit;
                hit.object = bit + 1;
                out.push_back(hit);
            }
        }
    }
};

interi::sdk::ModuleManifest Make(const char* id, ModuleKind kind)
{
    interi::sdk::ModuleManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.kind = kind;
    manifest.version = interi::sdk::ParseSemVer("1.0.0").value();
    return manifest;
}

interi::sdk::ModuleGraph ResolveFakeEngineProject()
{
    auto game = Make("com.test.game", ModuleKind::Module);
    game.physicsEngines.push_back({.id = "test.physics.fake", .version = {}, .isDefault = true, .library = {}});
    const std::vector available = {game, Make("test.physics.fake", ModuleKind::PhysicsEngine)};
    const std::vector<std::string> project = {"com.test.game"};
    return interi::sdk::ResolveModuleGraph(project, available, interi::sdk::CurrentHost()).value();
}

class PhysicsQueriesContract : public ::testing::Test
{
protected:
    interi::sdk::ModuleGraph graph = ResolveFakeEngineProject();
    ModuleSet modules{graph, interi::sdk::PhysicsContracts()};
    HookRegistry hooks{modules};
    FloorQueries floor;
};

} // namespace

TEST_F(PhysicsQueriesContract, EngineProvides_AgnosticModuleQueriesThroughHook)
{
    const ModuleScope engine{hooks, "test.physics.fake"};
    const auto declared = engine.DeclareHook<PhysicsQueriesService>(
        std::string(interi::sdk::kPhysicsQueriesContract),
        [this](const HookNext<PhysicsQueriesService>&) -> const IPhysicsQueries* { return &floor; });
    ASSERT_TRUE(declared.has_value()) << declared.error().message;

    const auto find = hooks.Find<PhysicsQueriesService>(interi::sdk::kPhysicsQueriesContract);
    ASSERT_TRUE(find.IsValid());
    const IPhysicsQueries* const queries = find();
    ASSERT_NE(queries, nullptr);

    const auto hit = queries->Raycast(Vec3(0.0f, 4.0f, 0.0f), Vec3(0.0f, -8.0f, 0.0f), interi::sdk::kAllPhysicsLayers);
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->fraction, 0.5f);
    EXPECT_EQ(hit->object, 7U);
    EXPECT_FALSE(queries->Raycast(Vec3(0.0f, 4.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), interi::sdk::kAllPhysicsLayers));
}

TEST_F(PhysicsQueriesContract, LayerMask_SelectsLayers)
{
    std::vector<PhysicsHit> out;
    floor.OverlapSphere(Vec3(0.0f, 0.0f, 0.0f), 1.0f, out, 0b101U);
    ASSERT_EQ(out.size(), 2U);
    EXPECT_EQ(out[0].object, 1U);
    EXPECT_EQ(out[1].object, 3U);
    EXPECT_FALSE(floor.Raycast(Vec3(0.0f, 4.0f, 0.0f), Vec3(0.0f, -8.0f, 0.0f), 0));
}

TEST_F(PhysicsQueriesContract, NonEngineProvider_Refused)
{
    const ModuleScope game{hooks, "com.test.game"};
    const auto result = game.DeclareHook<PhysicsQueriesService>(
        std::string(interi::sdk::kPhysicsQueriesContract),
        [this](const HookNext<PhysicsQueriesService>&) -> const IPhysicsQueries* { return &floor; });
    EXPECT_FALSE(result.has_value());
}
