// SPDX-License-Identifier: MIT
#pragma once

// Chain Functions: reusable sub-chains with typed inputs and outputs (.chainfn.json). A function is a chain
// (`nodes`, `links` and the editor data of ChainAsset) plus
//
//     "name": "Heal",
//     "inputs":  [{ "name": "amount", "type": "float" }],
//     "outputs": [{ "name": "Done", "payload": [{ "name": "healed", "type": "float" }] }]
//
// Registering a function (RegisterChainFunction) provides three kinds of events under its id `<provider>:<name>`:
//   <id>                the call node, with the function's inputs and outputs (what a chain uses);
//   <id>.Entry          the body's first node: its output "Out" carries the inputs as payload fields;
//   <id>.Return.<out>   the body's exits: its inputs are the output's payload fields, and it forwards them.
// A call is inlined before compiling (ExpandChainFunctions): the body's nodes are copied under the call's id as a
// prefix, the call's input bindings move to the Entry node, links into the call go to Entry, and links out of the
// call's output leave from every Return node of that output. The compiler and executor never see a call, so
// evaluation and determinism are untouched. Body nodes read the function's inputs as `payload.<input>` from the
// Entry node's payload (nodes linked directly from Entry); the body is otherwise an ordinary chain.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Event.h>
#include <interi/chain/Expression.h>
#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>
#include <interi/values/Value.h>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::chain
{

struct ChainFunctionInput
{
    std::string name;
    values::ValueType type = values::ValueType::Float;

    friend bool operator==(const ChainFunctionInput&, const ChainFunctionInput&) = default;
};

struct ChainFunctionOutput
{
    std::string name;
    std::vector<PayloadField> payload;

    friend bool operator==(const ChainFunctionOutput& a, const ChainFunctionOutput& b)
    {
        return a.name == b.name && a.payload.size() == b.payload.size() &&
               std::equal(a.payload.begin(), a.payload.end(), b.payload.begin(),
                          [](const PayloadField& x, const PayloadField& y)
                          { return x.name == y.name && x.type == y.type; });
    }
};

struct ChainFunction
{
    std::string name; ///< [A-Za-z0-9_].
    std::vector<ChainFunctionInput> inputs;
    std::vector<ChainFunctionOutput> outputs; ///< At least one.
    ChainAsset body;

    friend bool operator==(const ChainFunction&, const ChainFunction&) = default;
};

inline constexpr std::string_view kChainFunctionExtension = ".chainfn.json";

/// Event ids of function `name` provided by `provider`.
[[nodiscard]] std::string FunctionCallEvent(std::string_view provider, std::string_view name);
[[nodiscard]] std::string FunctionEntryEvent(std::string_view provider, std::string_view name);
[[nodiscard]] std::string FunctionReturnEvent(std::string_view provider, std::string_view name,
                                              std::string_view output);

/// InvalidArgument for a bad name, duplicate or invalid input/output/field names, or no outputs.
[[nodiscard]] Result<void> ValidateChainFunction(const ChainFunction& function);

[[nodiscard]] Result<ChainFunction> ParseChainFunction(std::string_view json);
[[nodiscard]] Result<ChainFunction> LoadChainFunction(const IFileSystem& files, std::string_view path);
/// Sorted keys; ParseChainFunction reads it back equal.
[[nodiscard]] std::string WriteChainFunction(const ChainFunction& function);

/// Provides the call, Entry and Return events of `function` as `provider` (a local id). Same errors as
/// EventRegistry::Provide, plus ValidateChainFunction's. Calling it again replaces nothing: use Override for a
/// changed function.
Result<void> RegisterChainFunction(ChainRegistry& registry, std::string_view provider, const ChainFunction& function);

/// Finds the function whose call event is `eventId`, or nullptr.
using FunctionLookup = std::function<const ChainFunction*(std::string_view eventId)>;

/// Inlines every call node (recursively, up to `kMaxFunctionDepth`), see the top of this header. `provider` is the id
/// the functions were registered under. InvalidArgument when a body lacks its Entry node, has a Return node for an
/// unknown output, or functions call each other too deeply (recursion).
[[nodiscard]] Result<ChainAsset> ExpandChainFunctions(const ChainAsset& asset, std::string_view provider,
                                                      const FunctionLookup& lookup);

inline constexpr int kMaxFunctionDepth = 8;

} // namespace interi::chain
