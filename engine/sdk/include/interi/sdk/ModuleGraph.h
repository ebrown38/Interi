// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/sdk/Manifest.h>

#include <cstddef>
#include <cstdint>
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
};

/// A project's modules after dependency resolution.
struct ModuleGraph
{
    /// Highest priority first: the project's order, with each implicit Extension directly below its lowest dependent.
    std::vector<ResolvedModule> modules;
    /// Indices into `modules`: every module after its dependencies, otherwise lowest priority first (so lower modules
    /// register before the ones that may override them).
    std::vector<std::size_t> loadOrder;

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
///   - a dependency cycle (InvalidArgument, the cycle in the message).
[[nodiscard]] Result<ModuleGraph> ResolveModuleGraph(std::span<const std::string> projectOrder,
                                                     std::span<const ModuleManifest> available, const HostInfo& host);

} // namespace interi::sdk
