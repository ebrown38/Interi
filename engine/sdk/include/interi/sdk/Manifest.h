// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/sdk/SemVer.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

/// What a module is for. Only Extensions may be depended on and may declare shared contract ids; Plugins only
/// override items other modules provide. A PhysicsEngine provides the reserved `service.Physics*` contracts; it is
/// never depended on or listed in a project, only selected from the `physicsEngines` other modules declare.
enum class ModuleKind : std::uint8_t
{
    Module,
    Extension,
    Plugin,
    PhysicsEngine,
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

/// A Physics Engine a module supports: the engine's module id, the versions it works with, and optionally the
/// module's own adapter library for that engine (base name, found next to module.json like `library`). An engine
/// that extends the named one satisfies the entry too (see SelectPhysicsEngine).
struct PhysicsEngineEntry
{
    std::string id;
    VersionRange version;
    bool isDefault = false; ///< Exactly one entry of a list of several; a single entry is the default.
    std::string library;
};

/// A named Physics Engine and the versions wanted: a PhysicsEngine's "extends" and the project's pin.
struct PhysicsEngineRef
{
    std::string id;
    VersionRange version;
};

/// A starter project a module ships: a folder (relative to the module) copied to make a new project.
struct ModuleTemplate
{
    std::string name;
    std::string path;
    std::string description;
};

/// An AI behaviour a module contributes (`behaviours`): a named, documented chain asset the module's computer players
/// run. Its id is `<module id>:<name>`.
struct AiBehaviourDecl
{
    std::string name;
    std::string description;
    std::string chain; ///< Chain asset inside the module (optional: a native behaviour has none).
};

/// A computer player a module offers (`computerPlayers`), id `<module id>:<name>`. `behaviour` is a local name of this
/// module or a full behaviour id of another one.
struct ComputerPlayerDecl
{
    std::string name;
    std::string behaviour;
    std::string description;
};

/// An animation set a module ships (`animationSets`, see anim/Animator.h): files inside the module and the character
/// archetype ids whose entities use it.
struct AnimationSetDecl
{
    std::string name;
    std::string skeleton;
    std::vector<std::string> clips;
    std::string graph;
    std::vector<std::string> archetypes;
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
///       "overrides": [ { "target": "com.author.base:FireGun", "version": "^1.0" } ],
///       "templates": [ { "name": "Coin game", "path": "templates/coins", "description": "..." } ],
///       "behaviours": [ { "name": "Patrol", "description": "...", "chain": "content/chains/patrol.chain.json" } ],
///       "computerPlayers": [ { "name": "Guard", "behaviour": "Patrol", "description": "..." } ],
///       "localization": [ "i18n/en.strings.json", "i18n/fr.strings.json" ],
///       "animationSets": [ { "name": "hero", "skeleton": "anim/hero.skeleton.json",
///                            "clips": ["anim/idle.clip.json"], "graph": "anim/hero.animstates.json",
///                            "archetypes": ["com.author.fps:Hero"] } ],
///       "physicsEngines": [ { "id": "com.interi.physics.jolt", "version": "^5", "default": true,
///                            "library": "fps_jolt" },
///                          { "id": "com.interi.physics.physx", "version": "^4" } ],
///       "extends": { "id": "com.interi.physics.jolt", "version": "^5" }
///     }
///
/// "physicsEngines" (Module, Extension, Plugin) lists the Physics Engines the module supports, exactly one marked
/// "default" when there are several; a module without it works with any engine. "extends" (PhysicsEngine only)
/// names the engine it builds on. A PhysicsEngine declares no "dependencies" and no "physicsEngines".
///
/// "id" and "version" are required. Missing metadata (name, kind, author, license, copyright, description, engine)
/// takes a default and adds a warning; the other fields default silently. Unknown top-level sections add a warning
/// (later engine versions add sections). Anything malformed is an error. The data sections ("values", "attributes",
/// "attributeSets", "archetypes", "dataTables") are known but parsed by interi::values (ModuleContent.h).
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
    std::vector<ModuleTemplate> templates;
    std::vector<AiBehaviourDecl> behaviours;
    std::vector<ComputerPlayerDecl> computerPlayers;
    std::vector<AnimationSetDecl> animationSets;
    std::vector<std::string> localization; ///< String table files inside the module (`.strings.json`).
    std::vector<PhysicsEngineEntry> physicsEngines;
    std::optional<PhysicsEngineRef> extends; ///< PhysicsEngine only.

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
