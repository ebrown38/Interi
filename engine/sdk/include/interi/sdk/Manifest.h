// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/sdk/SemVer.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

/// What a module is for. Only Extensions may be depended on and may declare shared contract ids; Plugins only
/// override items other modules provide.
enum class ModuleKind : std::uint8_t
{
    Module,
    Extension,
    Plugin,
};

[[nodiscard]] std::string_view ToString(ModuleKind kind) noexcept;

struct ModuleDependency
{
    std::string id;
    VersionRange version;
    bool optional = false; ///< Used when present; its absence is not an error.
};

/// A module that must not be loaded together with this one (when its version is in `version`).
struct ModuleConflict
{
    std::string id;
    VersionRange version;
};

/// An explicit override of a lower module's local item ("com.author.base:FireGun"). The owner module's version must
/// be in `version`.
struct OverrideDeclaration
{
    std::string target;
    VersionRange version;
};

/// A parsed module.json. The format:
///
///     {
///       "id": "com.author.fps", "name": "FPS", "version": "1.2.0", "kind": "Module",
///       "author": "...", "license": "MIT", "copyright": "...", "description": "...",
///       "icon": "icon.png", "homepage": "https://...", "engine": "^0.1",
///       "library": "fps",
///       "toolchain": "<fingerprint>",
///       "dependencies": [ { "id": "com.interi.physics", "version": "^1.0", "optional": false } ],
///       "conflicts": [ { "id": "com.other.fps", "version": "*" } ],
///       "contracts": [ "service.PhysicsQueries" ],
///       "overrides": [ { "target": "com.author.base:FireGun", "version": "^1.0" } ]
///     }
///
/// "id" and "version" are required. Missing metadata (name, kind, author, license, copyright, description, engine)
/// takes a default and adds a warning; the other fields default silently. Unknown top-level sections add a warning
/// (later engine versions add sections). Anything malformed is an error.
struct ModuleManifest
{
    std::string id; ///< Reverse-DNS, lowercase: "com.author.fps".
    std::string name;
    SemVer version;
    ModuleKind kind = ModuleKind::Module;
    std::string author;
    std::string license; ///< SPDX expression.
    std::string copyright;
    std::string description;
    std::string icon;
    std::string homepage;
    VersionRange engine;   ///< Engine versions it works with.
    std::string library;   ///< Shared library base name (no prefix or extension); empty for content-only modules.
    std::string toolchain; ///< Fingerprint the library was built with (see Toolchain.h); empty: not recorded.
    std::vector<ModuleDependency> dependencies;
    std::vector<ModuleConflict> conflicts;
    std::vector<std::string> contracts; ///< Shared contract ids this Extension declares.
    std::vector<OverrideDeclaration> overrides;

    std::filesystem::path directory; ///< Folder the manifest was loaded from (LoadManifest); empty when parsed.
    std::vector<std::string> warnings;
};

/// Parses module.json text (see ModuleManifest). InvalidArgument for malformed JSON or invalid fields.
[[nodiscard]] Result<ModuleManifest> ParseManifest(std::string_view json);

/// Reads `<directory>/module.json`. IoError if it can't be read.
[[nodiscard]] Result<ModuleManifest> LoadManifest(const std::filesystem::path& directory);

/// What the running engine offers a module.
struct HostInfo
{
    SemVer engineVersion;
    std::string toolchain; ///< EngineToolchainFingerprint() for the real host.
};

/// The running engine's HostInfo.
[[nodiscard]] HostInfo CurrentHost();

/// Checks that a module can be loaded into `host`: its engine range includes the host's version
/// (VersionMismatch) and a recorded toolchain fingerprint equals the host's (VersionMismatch, "toolchain").
[[nodiscard]] Result<void> CheckHostCompatibility(const ModuleManifest& manifest, const HostInfo& host);

/// True for a module id: two or more dot-separated lowercase labels of [a-z0-9_-], starting with a letter or digit.
[[nodiscard]] bool IsValidModuleId(std::string_view id) noexcept;

/// True for a local item id: "<module id>:<item name>" ("com.author.fps:FireGun").
[[nodiscard]] bool IsLocalId(std::string_view id) noexcept;

/// The owner of a local id ("com.author.fps" for "com.author.fps:FireGun"); empty for anything else.
[[nodiscard]] std::string_view OwnerOf(std::string_view id) noexcept;

/// True for an item name or shared id: letters, digits and "._-", not empty, not starting with '.'.
[[nodiscard]] bool IsValidItemName(std::string_view name) noexcept;

} // namespace interi::sdk
