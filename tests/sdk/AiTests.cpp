// SPDX-License-Identifier: MIT
#include <interi/sdk/Ai.h>
#include <interi/sdk/Manifest.h>

#include <gtest/gtest.h>

#include <format>

using namespace interi;
using namespace interi::sdk;

namespace
{

ModuleManifest Parse(const std::string& json)
{
    auto manifest = ParseManifest(json);
    EXPECT_TRUE(manifest.has_value()) << (manifest ? "" : manifest.error().message);
    return manifest ? std::move(*manifest) : ModuleManifest{};
}

std::string Module(const std::string& id, const std::string& sections)
{
    return std::format(R"({{"id": "{}", "version": "1.0.0", "name": "n", "kind": "Module", "author": "a",
        "license": "MIT", "copyright": "c", "description": "d", "engine": "*" {}}})",
                       id, sections.empty() ? "" : ", " + sections);
}

Result<ModuleGraph> Resolve(const std::vector<ModuleManifest>& manifests, std::vector<std::string> project)
{
    return ResolveModuleGraph(project, manifests, CurrentHost());
}

const char* kAiSections = R"("behaviours": [{"name": "Patrol", "description": "walk a loop",
                                             "chain": "content/chains/patrol.chain.json"}, {"name": "Flee"}],
    "computerPlayers": [{"name": "Guard", "behaviour": "Patrol"}, {"name": "Coward", "behaviour": "Flee"}])";

} // namespace

TEST(Manifest, AiAndAnimationSections_Parse)
{
    const ModuleManifest manifest = Parse(Module("com.test.ai", std::string(kAiSections) + R"(,
        "animationSets": [{"name": "hero", "skeleton": "anim/h.skeleton.json", "clips": ["anim/idle.clip.json"],
                           "graph": "anim/h.animstates.json", "archetypes": ["com.test.ai:Hero"]}])"));
    ASSERT_EQ(manifest.behaviours.size(), 2u);
    EXPECT_EQ(manifest.behaviours[0].chain, "content/chains/patrol.chain.json");
    EXPECT_EQ(manifest.computerPlayers[1].behaviour, "Flee");
    ASSERT_EQ(manifest.animationSets.size(), 1u);
    EXPECT_EQ(manifest.animationSets[0].archetypes, (std::vector<std::string>{"com.test.ai:Hero"}));
    for (const std::string& warning : manifest.warnings)
    {
        EXPECT_EQ(warning.find("behaviours"), std::string::npos) << warning;
        EXPECT_EQ(warning.find("animationSets"), std::string::npos) << warning;
    }
}

TEST(Manifest, AiSections_Malformed_Rejected)
{
    for (const char* sections : {
             R"("behaviours": [{"description": "no name"}])",
             R"("behaviours": [{"name": "a b"}])",
             R"("behaviours": [{"name": "X", "chain": "../escape.chain.json"}])",
             R"("computerPlayers": [{"name": "P"}])",
             R"("animationSets": [{"name": "s", "skeleton": "a", "graph": "/abs"}])",
             R"("animationSets": [{"name": "s", "skeleton": "a", "graph": "g", "clips": "c"}])",
             R"("animationSets": [{"name": "s", "skeleton": "a", "graph": "g", "archetypes": ["notlocal"]}])",
         })
    {
        EXPECT_FALSE(ParseManifest(Module("com.test.ai", sections)).has_value()) << sections;
    }
}

TEST(AiCatalog, Build_ListsBehavioursAndPlayersWithFullIds)
{
    const auto graph = Resolve({Parse(Module("com.test.ai", kAiSections))}, {"com.test.ai"});
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    auto catalog = AiCatalog::Build(*graph);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    ASSERT_EQ(catalog->Behaviours().size(), 2u);
    EXPECT_EQ(catalog->Behaviours()[0].id, "com.test.ai:Patrol");
    ASSERT_EQ(catalog->Players().size(), 2u);
    EXPECT_EQ(catalog->Players()[0].behaviour, "com.test.ai:Patrol");

    const auto request = catalog->MakeRequest("com.test.ai:Guard", 3);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->chain, "content/chains/patrol.chain.json");
    EXPECT_EQ(request->slot, 3u);
    EXPECT_EQ(catalog->MakeRequest("com.test.ai:Ghost", 0).error().code, ErrorCode::NotFound);
}

TEST(AiCatalog, PlayerUsesAnotherModulesBehaviour)
{
    const ModuleManifest base = Parse(Module("com.test.ai", kAiSections));
    const ModuleManifest game =
        Parse(Module("com.test.game", R"("computerPlayers": [{"name": "Bot", "behaviour": "com.test.ai:Patrol"}])"));
    const auto graph = Resolve({base, game}, {"com.test.game", "com.test.ai"});
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    const auto catalog = AiCatalog::Build(*graph);
    ASSERT_TRUE(catalog.has_value()) << catalog.error().message;
    EXPECT_NE(catalog->FindPlayer("com.test.game:Bot"), nullptr);
}

TEST(AiCatalog, Build_UnknownBehaviour_Rejected)
{
    const ModuleManifest game =
        Parse(Module("com.test.game", R"("computerPlayers": [{"name": "Bot", "behaviour": "Missing"}])"));
    const auto graph = Resolve({game}, {"com.test.game"});
    ASSERT_TRUE(graph.has_value());
    const auto catalog = AiCatalog::Build(*graph);
    ASSERT_FALSE(catalog.has_value());
    EXPECT_EQ(catalog.error().code, ErrorCode::InvalidArgument);
}

TEST(AiCatalog, Build_DuplicateBehaviour_Rejected)
{
    const ModuleManifest game = Parse(Module("com.test.game", R"("behaviours": [{"name": "A"}, {"name": "A"}])"));
    const auto graph = Resolve({game}, {"com.test.game"});
    ASSERT_TRUE(graph.has_value());
    EXPECT_FALSE(AiCatalog::Build(*graph).has_value());
}

TEST(AiHook, SpawnComputerPlayers_CallsTheGamesHookWithSlots)
{
    const ModuleManifest ai = Parse(Module("com.test.ai", kAiSections));
    const ModuleManifest game = Parse(Module("com.test.game", ""));
    const auto graph = Resolve({ai, game}, {"com.test.game", "com.test.ai"});
    ASSERT_TRUE(graph.has_value());
    const auto catalog = AiCatalog::Build(*graph);
    ASSERT_TRUE(catalog.has_value());

    const std::vector<std::string> contracts = AiContracts();
    const ModuleSet modules(*graph, contracts);
    HookRegistry hooks(modules);

    // Nobody provides the hook yet.
    EXPECT_EQ(SpawnComputerPlayers(*catalog, hooks, {"com.test.ai:Guard"}).error().code, ErrorCode::NotFound);

    std::vector<std::string> seen;
    ASSERT_TRUE(hooks
                    .Declare<SpawnComputerPlayerSignature>(
                        "com.test.game", kSpawnComputerPlayerHook,
                        [&seen](const HookNext<SpawnComputerPlayerSignature>&, const ComputerPlayerRequest& request)
                        {
                            seen.push_back(std::format("{}/{}/{}", request.player, request.behaviour, request.slot));
                            return static_cast<std::uint64_t>(100 + request.slot);
                        })
                    .has_value());
    const auto created = SpawnComputerPlayers(*catalog, hooks, {"com.test.ai:Guard", "com.test.ai:Coward"});
    ASSERT_TRUE(created.has_value()) << created.error().message;
    EXPECT_EQ(*created, (std::vector<std::uint64_t>{100, 101}));
    EXPECT_EQ(seen, (std::vector<std::string>{"com.test.ai:Guard/com.test.ai:Patrol/0",
                                              "com.test.ai:Coward/com.test.ai:Flee/1"}));

    // An unknown player fails before anything is spawned.
    seen.clear();
    EXPECT_FALSE(SpawnComputerPlayers(*catalog, hooks, {"com.test.ai:Guard", "com.test.ai:Nobody"}).has_value());
    EXPECT_TRUE(seen.empty());
}
