// SPDX-License-Identifier: MIT
#pragma once

// Pure functions that Chain Reaction expressions call by id ("fn.HasTag", "com.author.fps:IsHeadshot"). Modules
// register them like any contribution (sdk::ContributionRegistry), so a higher module or Plugin can replace one; an
// override must keep the signature. The engine's built-ins (min, max, abs, clamp, sqrt, floor, int, float, vec3,
// length, dot) are part of the language, not registry items.
//
// An implementation must be pure and thread-safe: chain waves evaluate expressions on job workers, and the result
// must depend only on the arguments (determinism).

#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/Value.h>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::chain
{

/// A function's implementation: receives arguments of the declared types and returns a value of the result type.
using FunctionImpl = std::function<values::Value(std::span<const values::Value> arguments)>;

/// One provider's definition of a function.
struct FunctionDefinition
{
    std::vector<values::ValueType> parameters;
    values::ValueType result = values::ValueType::Bool;
    std::string tooltip;
    std::shared_ptr<const FunctionImpl> implementation;
};

/// The functions of a project. Not thread-safe to modify; concurrent reads are fine while nothing modifies it.
class FunctionRegistry
{
public:
    using Entries = std::vector<std::pair<std::string, sdk::ContributionRegistry<FunctionDefinition>::Entry>>;

    /// `modules` must outlive the registry.
    explicit FunctionRegistry(const sdk::ModuleSet& modules)
        : m_registry(modules)
    {
    }

    /// `provider` provides function `id` (its local id or a declared shared id). InvalidArgument for a missing
    /// implementation, a List parameter or result, or the identity rule.
    Result<void> Provide(std::string_view provider, std::string_view id, FunctionDefinition definition);
    /// `provider` replaces a lower module's function `targetId`; the signature must match (InvalidArgument).
    Result<void> Override(std::string_view provider, std::string_view targetId, FunctionDefinition definition);

    /// The winning definition, or nullptr.
    [[nodiscard]] const FunctionDefinition* Resolve(std::string_view id) const noexcept
    {
        return m_registry.Resolve(id);
    }

    /// The underlying registry: Provenance, Subscribe, ExtractProvider/Restore.
    [[nodiscard]] sdk::ContributionRegistry<FunctionDefinition>& Registry() noexcept { return m_registry; }
    [[nodiscard]] const sdk::ContributionRegistry<FunctionDefinition>& Registry() const noexcept { return m_registry; }

private:
    sdk::ContributionRegistry<FunctionDefinition> m_registry;
};

} // namespace interi::chain
