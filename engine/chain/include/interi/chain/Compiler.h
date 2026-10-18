// SPDX-License-Identifier: MIT
#pragma once

// Compiles a chain asset against the project's registries into a flat program. Ids resolve through the registries,
// so a module's override of an event or function is what the program calls. Every binding and condition is
// type-checked once, per incoming link (a node's `payload.<field>` bindings are checked against each upstream
// output's payload). Nodes no trigger reaches are reported as warnings and left out.
//
// A program holds the implementations it calls (shared with the registry). Drop it when the registry changes
// (ChainExecutor does): a module's code must not outlive the module.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Event.h>
#include <interi/chain/Expression.h>
#include <interi/values/Value.h>
#include <interi/values/ValueRegistry.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace interi::chain
{

/// A compile message, located at a node, a link and/or an input, with an expression span when there is one.
struct Diagnostic
{
    enum class Severity : std::uint8_t
    {
        Error,
        Warning,
    };

    Severity severity = Severity::Error;
    std::string node;                ///< Node id; empty for none.
    std::optional<std::size_t> link; ///< Link index in the asset.
    std::string input;               ///< Input name, or "condition".
    std::string message;
    std::optional<SourceSpan> span; ///< Within the binding's or condition's expression text.

    /// "error: link 2 condition [4, 9): ..." style text.
    [[nodiscard]] std::string Text() const;
};

/// One link of a program: from an output of one node to an action node.
struct ProgramLink
{
    std::uint32_t target = 0;               ///< Node index.
    std::size_t assetLink = 0;              ///< Index in the asset (for the trace).
    CompiledExpression condition;           ///< Invalid: unconditional.
    std::vector<CompiledExpression> inputs; ///< One per target input, evaluated against this link's payload.
};

/// One node of a program.
struct ProgramNode
{
    std::string id;
    std::string event;
    EventKind kind = EventKind::Action;
    Authority authority = Authority::Any;
    std::shared_ptr<const EventImpl> implementation; ///< Actions.
    std::vector<values::Value> triggerInputs;        ///< Triggers: their constant inputs.
    std::size_t keyInput = 0;                        ///< Triggers: index of the "key" input, or triggerInputs.size().
    /// Per output: the [first, end) range of its links in ChainProgram::links, in asset order.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> outputLinks;
};

struct ChainProgram
{
    std::int32_t priority = 0;
    std::vector<ProgramNode> nodes; ///< Reachable nodes, in asset order.
    std::vector<ProgramLink> links;

    /// The index of node `id`, or nodes.size().
    [[nodiscard]] std::size_t NodeIndex(std::string_view id) const noexcept;
};

struct CompileResult
{
    std::shared_ptr<const ChainProgram> program; ///< nullptr if there was an error.
    std::vector<Diagnostic> diagnostics;
};

/// Compiles `asset`. `values` may be nullptr (no Value references then).
[[nodiscard]] CompileResult CompileChain(const ChainAsset& asset, const ChainRegistry& registry,
                                         const values::ValueRegistry* values);

} // namespace interi::chain
