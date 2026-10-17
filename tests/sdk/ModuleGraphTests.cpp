// SPDX-License-Identifier: MIT
#include <interi/sdk/ModuleGraph.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <string>
#include <vector>

using interi::ErrorCode;
using interi::sdk::HostInfo;
using interi::sdk::ModuleGraph;
using interi::sdk::ModuleKind;
using interi::sdk::ModuleManifest;
using interi::sdk::ResolveModuleGraph;

namespace
{

struct Dep
{
    const char* id;
    const char* range = "*";
    bool optional = false;
};

ModuleManifest Make(const char* id, ModuleKind kind, std::initializer_list<Dep> dependencies = {},
                    const char* version = "1.0.0")
{
    ModuleManifest manifest;
    manifest.id = id;
    manifest.name = id;
    manifest.kind = kind;
    manifest.version = interi::sdk::ParseSemVer(version).value();
    for (const Dep& dep : dependencies)
    {
        manifest.dependencies.push_back(
            {.id = dep.id, .version = interi::sdk::ParseVersionRange(dep.range).value(), .optional = dep.optional});
    }
    return manifest;
}

HostInfo Host()
{
    return HostInfo{.engineVersion = interi::sdk::ParseSemVer("0.1.0").value(), .toolchain = "test"};
}

interi::Result<ModuleGraph> Resolve(std::vector<std::string> project, const std::vector<ModuleManifest>& available)
{
    return ResolveModuleGraph(project, available, Host());
}

std::vector<std::string> PriorityIds(const ModuleGraph& graph)
{
    std::vector<std::string> ids;
    for (const auto& module : graph.modules)
    {
        ids.push_back(module.manifest.id);
    }
    return ids;
}

std::vector<std::string> LoadIds(const ModuleGraph& graph)
{
    std::vector<std::string> ids;
    for (const std::size_t index : graph.loadOrder)
    {
        ids.push_back(graph.modules[index].manifest.id);
    }
    return ids;
}

using Ids = std::vector<std::string>;

} // namespace

TEST(ModuleGraph, ProjectOrder_SetsPriority)
{
    const std::vector available = {Make("com.a.one", ModuleKind::Module), Make("com.a.two", ModuleKind::Module)};
    auto graph = Resolve({"com.a.two", "com.a.one"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.a.two", "com.a.one"}));
    EXPECT_EQ(graph->modules[0].priority, 1u);
    EXPECT_EQ(graph->modules[1].priority, 0u);
    // Without dependencies, the lowest priority registers first.
    EXPECT_EQ(LoadIds(*graph), (Ids{"com.a.one", "com.a.two"}));
    EXPECT_EQ(graph->Get("com.a.one")->manifest.id, "com.a.one");
    EXPECT_EQ(graph->Find("com.a.none"), ModuleGraph::npos);
}

TEST(ModuleGraph, ImplicitExtension_BelowDependent)
{
    // fps (top) and racer both need physics; physics needs math. Physics goes directly below its lowest dependent
    // (racer), math directly below physics.
    const std::vector available = {
        Make("com.a.fps", ModuleKind::Module, {{"com.x.physics"}}),
        Make("com.a.racer", ModuleKind::Module, {{"com.x.physics"}}),
        Make("com.a.ui", ModuleKind::Module),
        Make("com.x.physics", ModuleKind::Extension, {{"com.x.math"}}),
        Make("com.x.math", ModuleKind::Extension),
    };
    auto graph = Resolve({"com.a.fps", "com.a.racer", "com.a.ui"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.a.fps", "com.a.racer", "com.x.physics", "com.x.math", "com.a.ui"}));
    EXPECT_TRUE(graph->Get("com.x.physics")->implicit);
    EXPECT_FALSE(graph->Get("com.a.ui")->implicit);
    // Dependencies first; otherwise bottom-up.
    EXPECT_EQ(LoadIds(*graph), (Ids{"com.a.ui", "com.x.math", "com.x.physics", "com.a.racer", "com.a.fps"}));
}

TEST(ModuleGraph, ExplicitExtension_KeepsProjectPosition)
{
    const std::vector available = {Make("com.a.fps", ModuleKind::Module, {{"com.x.physics"}}),
                                   Make("com.x.physics", ModuleKind::Extension)};
    auto graph = Resolve({"com.x.physics", "com.a.fps"}, available);
    ASSERT_TRUE(graph.has_value()) << graph.error().message;
    EXPECT_EQ(PriorityIds(*graph), (Ids{"com.x.physics", "com.a.fps"}));
    EXPECT_FALSE(graph->modules[0].implicit);
    EXPECT_EQ(LoadIds(*graph), (Ids{"com.x.physics", "com.a.fps"})); // the dependency still loads first
}

TEST(ModuleGraph, Cycle_Error)
{
    const std::vector available = {
        Make("com.a.game", ModuleKind::Module, {{"com.x.one"}}),
        Make("com.x.one", ModuleKind::Extension, {{"com.x.two"}}),
        Make("com.x.two", ModuleKind::Extension, {{"com.x.one"}}),
    };
    auto graph = Resolve({"com.a.game"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(graph.error().message.find("cycle"), std::string::npos) << graph.error().message;
    EXPECT_NE(graph.error().message.find("com.x.one"), std::string::npos);
    EXPECT_NE(graph.error().message.find("com.x.two"), std::string::npos);
}

TEST(ModuleGraph, MissingDependency_Error)
{
    const std::vector available = {Make("com.a.game", ModuleKind::Module, {{"com.x.physics"}})};
    auto graph = Resolve({"com.a.game"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::NotFound);
    EXPECT_NE(graph.error().message.find("com.x.physics"), std::string::npos);
}

TEST(ModuleGraph, UnknownOrDuplicateProjectEntry_Error)
{
    const std::vector available = {Make("com.a.game", ModuleKind::Module)};
    EXPECT_EQ(Resolve({"com.a.other"}, available).error().code, ErrorCode::NotFound);
    EXPECT_EQ(Resolve({"com.a.game", "com.a.game"}, available).error().code, ErrorCode::InvalidArgument);
    const std::vector twice = {Make("com.a.game", ModuleKind::Module), Make("com.a.game", ModuleKind::Module)};
    EXPECT_EQ(Resolve({"com.a.game"}, twice).error().code, ErrorCode::InvalidArgument);
}

TEST(ModuleGraph, VersionMismatch_Error)
{
    const std::vector available = {Make("com.a.game", ModuleKind::Module, {{"com.x.physics", "^2.0"}}),
                                   Make("com.x.physics", ModuleKind::Extension, {}, "1.4.0")};
    auto graph = Resolve({"com.a.game"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::VersionMismatch);
}

TEST(ModuleGraph, DependencyOnNonExtension_Error)
{
    const std::vector available = {Make("com.a.game", ModuleKind::Module, {{"com.a.other"}}),
                                   Make("com.a.other", ModuleKind::Module)};
    EXPECT_EQ(Resolve({"com.a.game", "com.a.other"}, available).error().code, ErrorCode::InvalidArgument);
}

TEST(ModuleGraph, DeclaredConflict_Error)
{
    std::vector available = {Make("com.a.one", ModuleKind::Module), Make("com.a.two", ModuleKind::Module)};
    available[1].conflicts.push_back({.id = "com.a.one", .version = interi::sdk::ParseVersionRange("<2").value()});
    auto graph = Resolve({"com.a.one", "com.a.two"}, available);
    ASSERT_FALSE(graph.has_value());
    EXPECT_EQ(graph.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(graph.error().message.find("conflict"), std::string::npos);
    // Outside the conflicting range both load.
    available[0].version = interi::sdk::ParseSemVer("2.0.0").value();
    EXPECT_TRUE(Resolve({"com.a.one", "com.a.two"}, available).has_value());
}

TEST(ModuleGraph, OptionalDependency_UsedOnlyWhenPresent)
{
    const std::vector available = {Make("com.a.game", ModuleKind::Module, {{"com.x.audio", "^1", true}}),
                                   Make("com.x.audio", ModuleKind::Extension)};
    auto without = Resolve({"com.a.game"}, available);
    ASSERT_TRUE(without.has_value()) << without.error().message;
    EXPECT_EQ(PriorityIds(*without), (Ids{"com.a.game"}));

    auto with = Resolve({"com.a.game", "com.x.audio"}, available);
    ASSERT_TRUE(with.has_value()) << with.error().message;
    EXPECT_EQ(LoadIds(*with), (Ids{"com.x.audio", "com.a.game"}));

    const std::vector wrongVersion = {Make("com.a.game", ModuleKind::Module, {{"com.x.audio", "^2", true}}),
                                      Make("com.x.audio", ModuleKind::Extension)};
    EXPECT_EQ(Resolve({"com.a.game", "com.x.audio"}, wrongVersion).error().code, ErrorCode::VersionMismatch);
}

TEST(ModuleGraph, HostIncompatible_Error)
{
    std::vector available = {Make("com.a.game", ModuleKind::Module)};
    available[0].engine = interi::sdk::ParseVersionRange("^9").value();
    EXPECT_EQ(Resolve({"com.a.game"}, available).error().code, ErrorCode::VersionMismatch);
}
