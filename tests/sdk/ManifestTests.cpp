// SPDX-License-Identifier: MIT
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Toolchain.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

using interi::ErrorCode;
using interi::sdk::CheckHostCompatibility;
using interi::sdk::HostInfo;
using interi::sdk::ModuleKind;
using interi::sdk::ParseManifest;

namespace
{

constexpr const char* kFull = R"({
  "id": "com.author.fps", "name": "FPS", "version": "1.2.0", "kind": "Extension",
  "author": "Author", "license": "MIT", "copyright": "(c) Author", "description": "Shooting",
  "icon": "icon.png", "homepage": "https://example.com", "engine": "^0.1",
  "library": "fps",
  "dependencies": [ { "id": "com.interi.physics", "version": "^1.0" },
                    { "id": "com.interi.audio", "version": "*", "optional": true } ],
  "conflicts": [ { "id": "com.other.fps", "version": "<2" } ],
  "contracts": [ "service.Weapons", "event.Fired" ],
  "overrides": [ { "target": "com.author.base:FireGun", "version": "^1.0" } ]
})";

bool HasWarningContaining(const interi::sdk::ModuleManifest& manifest, std::string_view text)
{
    return std::ranges::any_of(manifest.warnings, [&](const std::string& w) { return w.find(text) != w.npos; });
}

HostInfo Host(std::string toolchain = "abi2;test")
{
    return HostInfo{.engineVersion = interi::sdk::ParseSemVer("0.1.4").value(), .toolchain = std::move(toolchain)};
}

} // namespace

TEST(Manifest, Full_ParsesEveryField)
{
    auto manifest = ParseManifest(kFull);
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;
    EXPECT_EQ(manifest->id, "com.author.fps");
    EXPECT_EQ(manifest->name, "FPS");
    EXPECT_EQ(manifest->version.ToString(), "1.2.0");
    EXPECT_EQ(manifest->kind, ModuleKind::Extension);
    EXPECT_EQ(manifest->license, "MIT");
    EXPECT_EQ(manifest->homepage, "https://example.com");
    EXPECT_EQ(manifest->library, "fps");
    ASSERT_EQ(manifest->dependencies.size(), 2u);
    EXPECT_EQ(manifest->dependencies[0].id, "com.interi.physics");
    EXPECT_FALSE(manifest->dependencies[0].optional);
    EXPECT_TRUE(manifest->dependencies[1].optional);
    ASSERT_EQ(manifest->conflicts.size(), 1u);
    EXPECT_EQ(manifest->contracts, (std::vector<std::string>{"service.Weapons", "event.Fired"}));
    ASSERT_EQ(manifest->overrides.size(), 1u);
    EXPECT_EQ(manifest->overrides[0].target, "com.author.base:FireGun");
    EXPECT_TRUE(manifest->warnings.empty());
}

TEST(Manifest, DataSections_NoWarning)
{
    auto manifest = ParseManifest(R"({ "id": "com.a.b", "name": "B", "version": "1.0.0", "kind": "Module",
        "author": "a", "license": "MIT", "copyright": "c", "description": "d", "engine": "*",
        "values": [], "attributes": [], "attributeSets": [], "archetypes": [], "dataTables": [] })");
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;
    EXPECT_TRUE(manifest->warnings.empty()) << manifest->warnings.front();
}

TEST(Manifest, MissingId_Error)
{
    auto manifest = ParseManifest(R"({ "version": "1.0.0" })");
    ASSERT_FALSE(manifest.has_value());
    EXPECT_EQ(manifest.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(manifest.error().message.find("id"), std::string::npos);
}

TEST(Manifest, MissingVersion_Error)
{
    EXPECT_FALSE(ParseManifest(R"({ "id": "com.a.b" })").has_value());
}

TEST(Manifest, MissingMetadata_DefaultsWithWarnings)
{
    auto manifest = ParseManifest(R"({ "id": "com.a.b", "version": "1.0.0" })");
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->name, "com.a.b");
    EXPECT_EQ(manifest->kind, ModuleKind::Module);
    EXPECT_TRUE(manifest->engine.MatchesAny());
    EXPECT_TRUE(HasWarningContaining(*manifest, "\"license\""));
    EXPECT_TRUE(HasWarningContaining(*manifest, "\"engine\""));
    EXPECT_FALSE(HasWarningContaining(*manifest, "\"homepage\""));
}

TEST(Manifest, UnknownSection_Warning)
{
    auto manifest = ParseManifest(R"({ "id": "com.a.b", "version": "1.0.0", "chains": [] })");
    ASSERT_TRUE(manifest.has_value());
    EXPECT_TRUE(HasWarningContaining(*manifest, "chains"));
}

TEST(Manifest, InvalidFields_Error)
{
    const char* const invalid[] = {
        "not json",
        R"([])",
        R"({ "id": "Com.A.B", "version": "1.0.0" })",                       // uppercase id
        R"({ "id": "single", "version": "1.0.0" })",                        // not reverse-DNS
        R"({ "id": "com.a.b", "version": "1.0" })",                         // bad semver
        R"({ "id": "com.a.b", "version": "1.0.0", "kind": "Game" })",       // unknown kind
        R"({ "id": "com.a.b", "version": "1.0.0", "engine": "^x" })",       // bad range
        R"({ "id": "com.a.b", "version": "1.0.0", "name": 3 })",            // wrong type
        R"({ "id": "com.a.b", "version": "1.0.0", "contracts": ["x.y"] })", // contracts outside an Extension
        R"({ "id": "com.a.b", "version": "1.0.0", "dependencies": [{ "id": "com.a.b" }] })", // self
        R"({ "id": "com.a.b", "version": "1.0.0", "dependencies": [{ "id": "com.c.d" }, { "id": "com.c.d" }] })",
        R"({ "id": "com.a.b", "version": "1.0.0", "overrides": [{ "target": "service.Shared" }] })", // not local
        R"({ "id": "com.a.b", "version": "1.0.0", "overrides": [{ "target": "com.a.b:Own" }] })",    // own item
        R"({ "id": "com.a.b", "version": "1.0.0", "kind": "Extension", "contracts": ["com.a.b:Local"] })",
        R"({ "id": "com.a.b", "version": "1.0.0", "library": "../evil" })",
    };
    for (const char* text : invalid)
    {
        auto manifest = ParseManifest(text);
        EXPECT_FALSE(manifest.has_value()) << text;
        if (!manifest.has_value())
        {
            EXPECT_EQ(manifest.error().code, ErrorCode::InvalidArgument) << text;
        }
    }
}

TEST(Manifest, FingerprintMismatch_Rejected)
{
    auto manifest = ParseManifest(R"({ "id": "com.a.b", "version": "1.0.0", "engine": "^0.1",
                                       "toolchain": "abi2;other-compiler" })");
    ASSERT_TRUE(manifest.has_value());
    auto result = CheckHostCompatibility(*manifest, Host());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::VersionMismatch);
    EXPECT_NE(result.error().message.find("toolchain"), std::string::npos);

    EXPECT_TRUE(CheckHostCompatibility(*manifest, Host("abi2;other-compiler")).has_value());
    manifest->toolchain.clear(); // not recorded: the library's own fingerprint is checked at load time
    EXPECT_TRUE(CheckHostCompatibility(*manifest, Host()).has_value());
}

TEST(Manifest, EngineRangeMismatch_Rejected)
{
    auto manifest = ParseManifest(R"({ "id": "com.a.b", "version": "1.0.0", "engine": "^0.2" })");
    ASSERT_TRUE(manifest.has_value());
    auto result = CheckHostCompatibility(*manifest, Host());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::VersionMismatch);
}

TEST(Manifest, CurrentHost_UsesEngineFingerprint)
{
    const HostInfo host = interi::sdk::CurrentHost();
    EXPECT_EQ(host.toolchain, interi::sdk::EngineToolchainFingerprint());
    // This test is compiled with the same toolchain as the engine.
    EXPECT_EQ(host.toolchain, interi::sdk::kToolchainFingerprint);
    EXPECT_EQ(host.toolchain.rfind("abi3;", 0), 0u);
}

TEST(Manifest, LoadManifest_ReadsDirectory)
{
    const interi::testing::ScopedTempDir dir;
    std::ofstream(dir.Path() / "module.json") << kFull;
    auto manifest = interi::sdk::LoadManifest(dir.Path());
    ASSERT_TRUE(manifest.has_value());
    EXPECT_EQ(manifest->directory, dir.Path());
    EXPECT_EQ(interi::sdk::LoadManifest(dir.Path() / "missing").error().code, ErrorCode::IoError);
}
