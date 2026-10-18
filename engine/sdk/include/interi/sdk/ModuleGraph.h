// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Project.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

/// A module in a resolved project.
struct ResolvedModule
{
    ModuleManifest manifest;
    /// Not in the project's list: pulled in as a dependency of a module that is.
    bool implicit = false;
    /// Higher wins wherever two modules provide the same item. The top of the project list has the highest value;
    /// values are dense (count-1 .. 0).
    std::uint32_t priority = 0;
    /// Ids of the modules that must load before this one: its dependencies present in the project, plus the
    /// Physics Engine edges (a module that declares `physicsEngines` after the selected engine, an engine after
    /// the one it extends). The host reloads dependents through the same edges.
    std::vector<std::string> loadAfter;
};

/// Why the project's Physics Engine was chosen.
enum class PhysicsEngineReason : std::uint8_t
{
    None,     ///< No engine: no module declares `physicsEngines` and the project pins none.
    Pinned,   ///< The project's pin.
    Default,  ///< The default entry of `physicsEngineChosenBy`.
    Fallback, ///< Another entry of `physicsEngineChosenBy`, because a lower module doesn't support its default.
};

[[nodiscard]] std::string_view ToString(PhysicsEngineReason reason) noexcept;

/// A project's modules after dependency resolution.
struct ModuleGraph
{
    /// Highest priority first: the project's order, with each implicit Extension directly below its lowest dependent.
    std::vector<ResolvedModule> modules;
    /// Indices into `modules`: every module after its dependencies, otherwise lowest priority first (so lower modules
    /// register before the ones that may override them).
    std::vector<std::size_t> loadOrder;
    /// The selected Physics Engine (index into `modules`, an implicit module of kind PhysicsEngine), or npos.
    std::size_t physicsEngine = npos;
    /// The selected engine first, then each engine it extends; empty without an engine.
    std::vector<std::size_t> physicsEngineChain;
    PhysicsEngineReason physicsEngineReason = PhysicsEngineReason::None;
    /// The module whose `physicsEngines` entry named the engine (Default and Fallback); empty otherwise.
    std::string physicsEngineChosenBy;

    /// Index into `modules`, or npos.
    [[nodiscard]] std::size_t Find(std::string_view id) const noexcept;
    /// The module with this id, or nullptr.
    [[nodiscard]] const ResolvedModule* Get(std::string_view id) const noexcept;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
};

/// Resolves a project's ordered module list (`projectOrder`, top = highest priority) against the modules found on
/// disk (`available`). Required dependencies are pulled in from `available`; an optional dependency is used only when
/// the project includes it by other means. All or nothing: any of these fails the whole project, with the reason:
///   - an unknown module, or a module listed twice (NotFound / InvalidArgument);
///   - a missing required dependency (NotFound), or a dependency on something other than an Extension
///     (InvalidArgument);
///   - a dependency whose version is outside the required range, or a module the engine can't load (see
///     CheckHostCompatibility) (VersionMismatch);
///   - a declared conflict between two included modules (InvalidArgument);
///   - a dependency cycle (InvalidArgument, the cycle in the message);
///   - a PhysicsEngine listed in the project or depended on (InvalidArgument), or no Physics Engine every module
///     that declares `physicsEngines` supports (VersionMismatch, NotFound when none is installed; see
///     SelectPhysicsEngine).
/// `available` may hold several versions of one PhysicsEngine; any other id must be unique. `physicsEngine` is the
/// project's pin. The selected engine joins the graph as an implicit module, with the engines it extends directly
/// below it, below the lowest-priority module that declares `physicsEngines`; it loads before them.
[[nodiscard]] Result<ModuleGraph> ResolveModuleGraph(std::span<const std::string> projectOrder,
                                                     std::span<const ModuleManifest> available, const HostInfo& host,
                                                     const std::optional<PhysicsEngineRef>& physicsEngine = {});

/// The engine that won, with the engines it extends, and why.
struct PhysicsEngineSelection
{
    /// The selected engine first, then each engine it extends (base versions as resolved).
    std::vector<const ModuleManifest*> chain;
    PhysicsEngineReason reason = PhysicsEngineReason::None;
    std::string chosenBy;
};

/// True if the engine `chain` (selected engine first, then its bases) satisfies `entry`: the engine itself or any
/// base in the chain has the entry's id and a version inside its range.
[[nodiscard]] bool Satisfies(std::span<const ModuleManifest* const> chain, const PhysicsEngineEntry& entry) noexcept;

/// Picks the project's Physics Engine from `modules` (the project's modules, highest priority first; those with
/// `physicsEngines` constrain the choice) and the engines installed in `available`:
///   1. Candidates: the pin alone, else every engine the declaring modules name, in priority order (a module's
///      default entry first, then its others in declared order).
///   2. The first candidate every declaring module supports wins, at the highest installed version inside every
///      applicable range (and the pin's); an engine that extends a named one counts as it.
/// Returns an empty optional when nothing constrains and nothing is pinned. Errors: VersionMismatch (no candidate
/// works; the message lists each with the modules blocking it), NotFound (no version of any candidate installed, or
/// a base engine missing), InvalidArgument (an `extends` cycle, or one naming a module that isn't an engine).
[[nodiscard]] Result<std::optional<PhysicsEngineSelection>>
SelectPhysicsEngine(std::span<const ModuleManifest* const> modules, std::span<const ModuleManifest> available,
                    const std::optional<PhysicsEngineRef>& pin, const HostInfo& host);

/// Resolves `project` with `candidate` added at `position` in its module order (0 = highest priority) and returns
/// why it would be refused, or success. `available` is every installed module (the candidate is added to it if it
/// isn't there); the project is not changed.
[[nodiscard]] Result<void> CanAddModule(const Project& project, std::span<const ModuleManifest> available,
                                        const ModuleManifest& candidate, const HostInfo& host,
                                        std::size_t position = 0);

} // namespace interi::sdk
