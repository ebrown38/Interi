// SPDX-License-Identifier: MIT
#include <interi/sdk/ModuleGraph.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

using interi::ErrorCode;
using interi::sdk::HostInfo;
using interi::sdk::ModuleGraph;
using interi::sdk::ModuleKind;
using interi::sdk::ModuleManifest;
using interi::sdk::PhysicsEngineReason;
using interi::sdk::PhysicsEngineRef;

namespace
{

constexpr const char* kJolt = "test.physics.alpha";
constexpr const char* kPhysx = "test.physics.beta";
constexpr const char* kJoltPlus = "test.physics.alpha_plus";

struct Support
{
    const char* id;
    const char* range = "*";
    bool isDefault = false;
};

ModuleManifest Engine(const char* id, const char* version, const char* extends = nullptr,
                      const char* extendsRange = "*")
{
    ModuleManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.kind = ModuleKind::PhysicsEngine;
    manifest.version = interi::sdk::ParseSemVer(version).value();
    if (extends != nullptr)
    {
        manifest.extends =
            PhysicsEngineRef{.id = extends, .version = interi::sdk::ParseVersionRange(extendsRange).value()};
    }
    return manifest;
}

ModuleManifest Module(const char* id, std::initializer_list<Support> engines = {}, ModuleKind kind = ModuleKind::Module)
{
    ModuleManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.kind = kind;
    manifest.version = interi::sdk::ParseSemVer("1.0.0").value();
    for (const Support& support : engines)
    {
        manifest.physicsEngines.push_back({.id = support.id,
                                           .version = interi::sdk::ParseVersionRange(support.range).value(),
                                           .isDefault = support.isDefault || engines.size() == 1,
                                           .library = {}});
    }
    return manifest;
}

HostInfo Host()
{
    return HostInfo{.engineVersion = interi::sdk::ParseSemVer("0.1.0").value(), .toolchain = "test"};
}

interi::Result<ModuleGraph> Resolve(std::vector<std::string> project, const std::vector<ModuleManifest>& available,
                                    std::optional<PhysicsEngineRef> pin = std::nullopt)
{
    return interi::sdk::ResolveModuleGraph(project, available, Host(), pin);
}

PhysicsEngineRef Pin(const char* id, const char* range = "*")
{
    return PhysicsEngineRef{.id = id, .version = interi::sdk::ParseVersionRange(range).value()};
}

using Ids = std::vector<std::string>;

Ids PriorityIds(const ModuleGraph& graph)
{
    Ids ids;
    for (const auto& module : graph.modules)
    {
        ids.push_back(module.manifest.id);
    }
    return ids;
}

Ids LoadIds(const ModuleGraph& graph)
{
    Ids ids;
    for (const std::size_t index : graph.loadOrder)
    {
        ids.push_back(graph.modules[index].manifest.id);
    }
    return ids;
}

std::string SelectedId(const ModuleGraph& graph)
{
    return graph.physicsEngine == ModuleGraph::npos ? std::string{} : graph.modules[graph.physicsEngine].manifest.id;
}

std::string SelectedVersion(const ModuleGraph& graph)
{
    return graph.physicsEngine == ModuleGraph::npos ? std::string{}
                                                    : graph.modules[graph.physicsEngine].manifest.version.ToString();
}

} // namespace

TEST(PhysicsEngine, FirstDeclaringModuleDefault_Selected)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "*", true}, {kPhysx}}),
                                   Module("com.t.c", {{kPhysx}, {kJolt, "*", true}}), Engine(kJolt, "5.0.0"),
                                   Engine(kPhysx, "4.0.0")};
    // Both support both engines; the top module's default (Jolt) is tried first.
    auto both = Resolve({"com.t.a", "com.t.c"}, available);
    ASSERT_TRUE(both.has_value()) << both.error().message;
    EXPECT_EQ(SelectedId(*both), kJolt);
    EXPECT_EQ(both->physicsEngineReason, PhysicsEngineReason::Default);
    EXPECT_EQ(both->physicsEngineChosenBy, "com.t.a");
}

TEST(PhysicsEngine, DefaultUnsupportedByLower_FallsBackToCommonEngine)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "*", true}, {kPhysx}}), Module("com.t.b", {{kPhysx}}),
                                   Engine(kJolt, "5.0.0"), Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.a", "com.t.b"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kPhysx);
    EXPECT_EQ(graph->physicsEngineReason, PhysicsEngineReason::Fallback);
    EXPECT_EQ(graph->physicsEngineChosenBy, "com.t.a");
    EXPECT_TRUE(graph->modules[graph->physicsEngine].implicit);
}

TEST(PhysicsEngine, NoCommonEngine_ProjectRejected_NamesBlockers)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Module("com.t.b", {{kPhysx}}), Engine(kJolt, "5.0.0"),
                                   Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.a", "com.t.b"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::VersionMismatch);
    const std::string& message = graph.error().message;
    EXPECT_NE(message.find(kJolt), std::string::npos) << message;
    EXPECT_NE(message.find(kPhysx), std::string::npos) << message;
    EXPECT_NE(message.find("com.t.b"), std::string::npos) << message; // blocks Jolt
    EXPECT_NE(message.find("com.t.a"), std::string::npos) << message; // blocks PhysX
}

TEST(PhysicsEngine, VersionRanges_HighestCommonVersion)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "^5"}}),
                                   Module("com.t.b", {{kJolt, ">=4 <5.1"}}),
                                   Engine(kJolt, "4.0.0"),
                                   Engine(kJolt, "5.0.0"),
                                   Engine(kJolt, "5.1.0"),
                                   Engine(kJolt, "6.0.0")};
    auto graph = Resolve({"com.t.a", "com.t.b"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedVersion(*graph), "5.0.0");

    auto alone = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(alone.has_value());
    EXPECT_EQ(SelectedVersion(*alone), "5.1.0");
}

TEST(PhysicsEngine, DisjointVersionRanges_Rejected)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "^4"}}), Module("com.t.b", {{kJolt, "^6"}}),
                                   Engine(kJolt, "4.2.0"), Engine(kJolt, "6.1.0")};
    auto graph = Resolve({"com.t.a", "com.t.b"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::VersionMismatch);
}

TEST(PhysicsEngine, NoVersionInstalled_NotFound)
{
    const std::vector available = {Module("com.t.a", {{kJolt}})};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::NotFound);
    EXPECT_NE(graph.error().message.find(kJolt), std::string::npos);
}

TEST(PhysicsEngine, ProjectPin_OverridesDefault)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "*", true}, {kPhysx}}), Engine(kJolt, "5.0.0"),
                                   Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.a"}, available, Pin(kPhysx));
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kPhysx);
    EXPECT_EQ(graph->physicsEngineReason, PhysicsEngineReason::Pinned);

    auto ranged = Resolve({"com.t.a"}, available, Pin(kJolt, "^6"));
    EXPECT_FALSE(ranged.has_value()); // the pin's range excludes the installed version
}

TEST(PhysicsEngine, PinUnsupportedByModule_Rejected)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Module("com.t.b", {{kJolt}, {kPhysx}}),
                                   Engine(kJolt, "5.0.0"), Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.a", "com.t.b"}, available, Pin(kPhysx));
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::VersionMismatch);
    EXPECT_NE(graph.error().message.find("com.t.a"), std::string::npos) << graph.error().message;
}

TEST(PhysicsEngine, PinWithoutDeclaringModules_SelectsPinnedEngine)
{
    const std::vector available = {Module("com.t.a"), Engine(kJolt, "5.0.0")};
    auto graph = Resolve({"com.t.a"}, available, Pin(kJolt));
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a", kJolt}));
}

TEST(PhysicsEngine, NoDeclaringModules_NoEngine)
{
    const std::vector available = {Module("com.t.a"), Engine(kJolt, "5.0.0")};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(graph->physicsEngine, ModuleGraph::npos);
    EXPECT_EQ(graph->physicsEngineReason, PhysicsEngineReason::None);
    EXPECT_TRUE(graph->physicsEngineChain.empty());
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a"}));
}

TEST(PhysicsEngine, AgnosticModules_DoNotConstrain)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Module("com.t.agnostic"), Engine(kJolt, "5.0.0"),
                                   Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.agnostic", "com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kJolt);
}

TEST(PhysicsEngine, PlacedBelowLowestDeclaringModule)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Module("com.t.c"), Module("com.t.b", {{kJolt}}),
                                   Module("com.t.d"), Engine(kJolt, "5.0.0")};
    auto graph = Resolve({"com.t.a", "com.t.c", "com.t.b", "com.t.d"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a", "com.t.c", "com.t.b", kJolt, "com.t.d"}));
    for (std::size_t i = 0; i < graph->modules.size(); ++i)
    {
        EXPECT_EQ(graph->modules[i].priority, graph->modules.size() - 1 - i);
    }
    // The engine loads before the modules that declare it.
    const Ids load = LoadIds(*graph);
    const auto at = [&](const char* id) { return std::ranges::find(load, id) - load.begin(); };
    EXPECT_LT(at(kJolt), at("com.t.a"));
    EXPECT_LT(at(kJolt), at("com.t.b"));
}

TEST(PhysicsEngine, ExtendingEngine_SatisfiesBaseEntries)
{
    // A supports Jolt; C supports only "alpha_plus", which extends Jolt 5.
    const std::vector available = {Module("com.t.a", {{kJolt, "^5"}}), Module("com.t.c", {{kJoltPlus}}),
                                   Engine(kJolt, "5.2.0"), Engine(kJoltPlus, "1.0.0", kJolt, "^5")};
    auto graph = Resolve({"com.t.a", "com.t.c"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kJoltPlus);
    ASSERT_EQ(graph->physicsEngineChain.size(), 2U);
    EXPECT_EQ(graph->modules[graph->physicsEngineChain[1]].manifest.id, kJolt);
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a", "com.t.c", kJoltPlus, kJolt}));
}

TEST(PhysicsEngine, ExtendsChain_BasesLoadedBelowExtender)
{
    const char* gamma = "test.physics.gamma";
    const std::vector available = {Module("com.t.a", {{gamma}}), Engine(kJolt, "1.0.0"), Engine(kPhysx, "1.0.0", kJolt),
                                   Engine(gamma, "1.0.0", kPhysx)};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a", gamma, kPhysx, kJolt}));
    EXPECT_EQ(LoadIds(*graph), (Ids{kJolt, kPhysx, gamma, "com.t.a"}));
    ASSERT_EQ(graph->physicsEngineChain.size(), 3U);
    EXPECT_EQ(graph->physicsEngine, graph->physicsEngineChain.front());
}

TEST(PhysicsEngine, InstalledExtenderNotNamed_NotSelected)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Engine(kJolt, "5.0.0"),
                                   Engine(kJoltPlus, "1.0.0", kJolt)};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kJolt);
    EXPECT_EQ(graph->physicsEngineChain.size(), 1U);
}

TEST(PhysicsEngine, ExtendsBaseVersionOutsideRange_NotSatisfied)
{
    // The extender builds on Jolt 2.x; A wants Jolt ^1, so the extender can't stand in for it.
    const std::vector available = {Module("com.t.a", {{kJolt, "^1"}}), Module("com.t.c", {{kJoltPlus}}),
                                   Engine(kJolt, "1.0.0"), Engine(kJolt, "2.0.0"),
                                   Engine(kJoltPlus, "1.0.0", kJolt, "^2")};
    auto graph = Resolve({"com.t.a", "com.t.c"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::VersionMismatch);
}

TEST(PhysicsEngine, ExtenderBaseVersion_HighestSatisfyingChain)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "<1.5"}}), Module("com.t.c", {{kJoltPlus}}),
                                   Engine(kJolt, "1.0.0"), Engine(kJolt, "1.5.0"),
                                   Engine(kJoltPlus, "1.0.0", kJolt, "^1")};
    auto graph = Resolve({"com.t.a", "com.t.c"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(graph->modules[graph->physicsEngineChain[1]].manifest.version.ToString(), "1.0.0");

    auto free = Resolve({"com.t.c"}, available);
    ASSERT_TRUE(free.has_value());
    EXPECT_EQ(free->modules[free->physicsEngineChain[1]].manifest.version.ToString(), "1.5.0");
}

TEST(PhysicsEngine, ExtendsCycle_Error)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Engine(kJolt, "1.0.0", kPhysx),
                                   Engine(kPhysx, "1.0.0", kJolt)};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(graph.error().message.find("cycle"), std::string::npos) << graph.error().message;
}

TEST(PhysicsEngine, ExtendsNonEngineOrMissing_Error)
{
    const std::vector nonEngine = {Module("com.t.a", {{kJolt}}), Module("com.t.base"),
                                   Engine(kJolt, "1.0.0", "com.t.base")};
    auto bad = Resolve({"com.t.a"}, nonEngine);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code, ErrorCode::InvalidArgument);

    const std::vector missing = {Module("com.t.a", {{kJolt}}), Engine(kJolt, "1.0.0", kPhysx)};
    auto none = Resolve({"com.t.a"}, missing);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error().code, ErrorCode::NotFound);
}

TEST(Manifest, DependsOnPhysicsEngine_Error)
{
    ModuleManifest module = Module("com.t.a");
    module.dependencies.push_back({.id = kJolt, .version = {}, .optional = false});
    auto graph = Resolve({"com.t.a"}, std::vector{module, Engine(kJolt, "1.0.0")});
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(graph.error().message.find("physicsEngines"), std::string::npos) << graph.error().message;
}

TEST(Manifest, PhysicsEngineInProjectOrder_Error)
{
    auto graph = Resolve({"com.t.a", kJolt}, std::vector{Module("com.t.a"), Engine(kJolt, "1.0.0")});
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(graph.error().message.find("physicsEngines"), std::string::npos) << graph.error().message;
}

TEST(PhysicsEngine, SeveralVersionsOfOtherKinds_StillDuplicateError)
{
    auto graph = Resolve({"com.t.a"}, std::vector{Module("com.t.a"), Module("com.t.a")});
    ASSERT_FALSE(graph.has_value());
    auto sameEngine =
        Resolve({"com.t.a"}, std::vector{Module("com.t.a", {{kJolt}}), Engine(kJolt, "1.0.0"), Engine(kJolt, "1.0.0")});
    EXPECT_FALSE(sameEngine.has_value());
}

TEST(PhysicsEngine, IncompatibleHostVersion_Skipped)
{
    ModuleManifest newer = Engine(kJolt, "6.0.0");
    newer.engine = interi::sdk::ParseVersionRange("^9").value();
    const std::vector available = {Module("com.t.a", {{kJolt}}), Engine(kJolt, "5.0.0"), newer};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedVersion(*graph), "5.0.0");
}

TEST(PhysicsEngine, ImplicitExtensionDeclaring_Participates)
{
    ModuleManifest extension = Module("com.t.ext", {{kPhysx}}, ModuleKind::Extension);
    ModuleManifest user = Module("com.t.a", {{kJolt, "*", true}, {kPhysx}});
    user.dependencies.push_back({.id = "com.t.ext", .version = {}, .optional = false});
    const std::vector available = {user, extension, Engine(kJolt, "5.0.0"), Engine(kPhysx, "4.0.0")};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(SelectedId(*graph), kPhysx);
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.t.a", "com.t.ext", kPhysx}));
    const Ids load = LoadIds(*graph);
    EXPECT_EQ(load.front(), kPhysx);
}

TEST(PhysicsEngine, EffectiveLoadEdges_ListEngineAndBase)
{
    const std::vector available = {Module("com.t.a", {{kJoltPlus}}), Engine(kJolt, "1.0.0"),
                                   Engine(kJoltPlus, "1.0.0", kJolt)};
    auto graph = Resolve({"com.t.a"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(graph->Get("com.t.a")->loadAfter, Ids{kJoltPlus});
    EXPECT_EQ(graph->Get(kJoltPlus)->loadAfter, Ids{kJolt});
    EXPECT_TRUE(graph->Get(kJolt)->loadAfter.empty());
}

TEST(CanAddModule, IncompatiblePhysicsEngine_RefusedProjectUnchanged)
{
    const std::vector available = {Module("com.t.a", {{kJolt}}), Engine(kJolt, "5.0.0"), Engine(kPhysx, "4.0.0")};
    interi::sdk::Project project;
    project.name = "P";
    project.modules = {"com.t.a"};
    const interi::sdk::Project before = project;

    const ModuleManifest incompatible = Module("com.t.b", {{kPhysx}});
    auto refused = interi::sdk::CanAddModule(project, available, incompatible, Host());
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::VersionMismatch);
    EXPECT_NE(refused.error().message.find("com.t.b"), std::string::npos);
    EXPECT_EQ(project, before);

    const ModuleManifest compatible = Module("com.t.c", {{kJolt}});
    EXPECT_TRUE(interi::sdk::CanAddModule(project, available, compatible, Host()).has_value());
    EXPECT_EQ(project, before);

    // Adding a module the project already lists is refused too.
    EXPECT_FALSE(interi::sdk::CanAddModule(project, available, available[0], Host()).has_value());
}

TEST(CanAddModule, AtPosition_ChangesPriority)
{
    const std::vector available = {Module("com.t.a", {{kJolt, "*", true}, {kPhysx}}), Engine(kJolt, "5.0.0"),
                                   Engine(kPhysx, "4.0.0")};
    interi::sdk::Project project;
    project.modules = {"com.t.a"};
    EXPECT_TRUE(interi::sdk::CanAddModule(project, available, Module("com.t.b", {{kPhysx}}), Host(), 0).has_value());
    EXPECT_TRUE(interi::sdk::CanAddModule(project, available, Module("com.t.b", {{kPhysx}}), Host(), 1).has_value());
}
