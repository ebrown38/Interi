// SPDX-License-Identifier: MIT
#pragma once

// The chain asset format (.chain.json): event nodes, and links ("chains") from a node's output to another node.
// Authored as JSON until the visual editor (M22) writes it; keys are written sorted so files diff well.
//
//     {
//       "version": 1,
//       "priority": 0,
//       "nodes": [
//         { "id": "fire",  "event": "trigger.InputAction", "params": { "key": { "const": "input.Fire" } } },
//         { "id": "shoot", "event": "com.author.fps:FireGun",
//           "params": { "range": { "value": "com.author.fps:GunRange" } } },
//         { "id": "hurt",  "event": "world.ModifyValue",
//           "params": { "id":    { "const": "com.author.fps:Health" },
//                       "delta": { "expr": "-payload.damage * 2" } } }
//       ],
//       "links": [
//         { "from": "fire", "to": "shoot" },
//         { "from": "shoot.Hit", "to": "hurt", "condition": "payload.target == self" }
//       ]
//     }
//
// A binding is one of {"const": <json>} (parsed with the input's schema), {"payload": "<field>"} (of the upstream
// output's payload), {"value": "<id>"} or {"expr": "<expression>"}. An unbound input takes its default. A link's
// "from" is "<node>.<output>", or "<node>" for the event's first output; its optional "condition" is a bool
// expression over the upstream payload and Values that makes the link an "if". Node ids are names
// ([A-Za-z0-9_], unique). Malformed content is an error naming where; unknown keys are warnings.

#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::chain
{

/// The current .chain.json version.
inline constexpr std::uint32_t kChainFormatVersion = 1;

/// Where a node input's value comes from.
struct Binding
{
    enum class Kind : std::uint8_t
    {
        Constant,   ///< `text` is JSON.
        Payload,    ///< `text` is a payload field name.
        Value,      ///< `text` is a Value id.
        Expression, ///< `text` is an expression.
    };

    Kind kind = Kind::Constant;
    std::string text;

    friend bool operator==(const Binding&, const Binding&) = default;
};

struct ChainNode
{
    std::string id;
    std::string event;
    std::vector<std::pair<std::string, Binding>> params; ///< Input name -> binding, sorted by name.

    friend bool operator==(const ChainNode&, const ChainNode&) = default;
};

struct ChainLink
{
    std::string fromNode;
    std::string fromOutput; ///< Empty: the event's first output.
    std::string toNode;
    std::string condition; ///< Empty: unconditional.

    friend bool operator==(const ChainLink&, const ChainLink&) = default;
};

struct ChainAsset
{
    /// Orders this chain's activations against other chains' in a step (lower first).
    std::int32_t priority = 0;
    std::vector<ChainNode> nodes;
    std::vector<ChainLink> links;
    std::vector<std::string> warnings; ///< From parsing; not written.

    friend bool operator==(const ChainAsset& a, const ChainAsset& b)
    {
        return a.priority == b.priority && a.nodes == b.nodes && a.links == b.links;
    }
};

/// Parses .chain.json text. InvalidArgument (with the node or link index) for malformed content, VersionMismatch for
/// a newer version.
[[nodiscard]] Result<ChainAsset> ParseChain(std::string_view json);
/// Reads and parses a chain asset.
[[nodiscard]] Result<ChainAsset> LoadChain(const IFileSystem& files, std::string_view path);
/// The asset as .chain.json text (sorted keys, two-space indent); ParseChain reads it back equal.
[[nodiscard]] std::string WriteChain(const ChainAsset& asset);

} // namespace interi::chain
