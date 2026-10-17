// SPDX-License-Identifier: MIT
#pragma once

// The shared test project (tests/sdk/modules): Extension com.test.ext declares service.Greeter, input.Jump and
// attr.Health; modules com.test.a (1.2.0) and com.test.b (2.0.0) depend on it; B declares an override of A's
// com.test.a:Fire; Plugin com.test.plugin (0.3.0) declares overrides of com.test.a:Fire and com.test.b:Reload.
// Project order (top first): plugin, b, a; ext is pulled in below a. Engine contract: event.Damage. The manifests also
// carry data sections (Values, characters, the com.test.a:Guns table); tests/values describes them.

#include <interi/sdk/ContributionRegistry.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace interi::testing
{

inline const std::filesystem::path kSdkTestModulesDir = INTERI_SDK_TEST_MODULES_DIR;

inline std::vector<sdk::ModuleManifest> LoadSdkTestManifests()
{
    std::vector<sdk::ModuleManifest> manifests;
    for (const char* name : {"ext", "a", "b", "plugin"})
    {
        auto manifest = sdk::LoadManifest(kSdkTestModulesDir / name);
        EXPECT_TRUE(manifest.has_value()) << name << ": " << (manifest ? "" : manifest.error().message);
        if (manifest)
        {
            manifests.push_back(std::move(*manifest));
        }
    }
    return manifests;
}

inline sdk::ModuleGraph ResolveSdkTestProject(std::vector<std::string> project = {"com.test.plugin", "com.test.b",
                                                                                  "com.test.a"})
{
    const std::vector<sdk::ModuleManifest> manifests = LoadSdkTestManifests();
    auto graph = sdk::ResolveModuleGraph(project, manifests, sdk::CurrentHost());
    EXPECT_TRUE(graph.has_value()) << (graph ? "" : graph.error().message);
    return graph.value_or(sdk::ModuleGraph{});
}

inline const std::vector<std::string> kSdkTestEngineContracts = {"event.Damage"};

} // namespace interi::testing
