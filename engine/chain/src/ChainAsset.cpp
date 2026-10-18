// SPDX-License-Identifier: MIT
#include <interi/chain/ChainAsset.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>

namespace interi::chain
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

[[nodiscard]] bool IsName(std::string_view name)
{
    return !name.empty() &&
           std::ranges::all_of(
               name, [](char c)
               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

void WarnUnknownKeys(const Json& object, std::span<const std::string_view> known, std::string_view where,
                     std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : object.items())
    {
        if (std::ranges::find(known, key) == known.end())
        {
            warnings.push_back(std::format("{}: unknown key '{}'", where, key));
        }
    }
}

Result<std::string> GetString(const Json& object, std::string_view key, std::string_view where, bool required)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        if (required)
        {
            return Invalid(std::format("{}: missing \"{}\"", where, key));
        }
        return std::string{};
    }
    if (!it->is_string())
    {
        return Invalid(std::format("{}: \"{}\" must be a string", where, key));
    }
    return it->get<std::string>();
}

constexpr std::string_view kBindingKinds[] = {"const", "payload", "value", "expr"};

Result<Binding> ParseBinding(const Json& json, std::string_view where)
{
    if (!json.is_object() || json.size() != 1)
    {
        return Invalid(
            std::format(R"({}: a binding is an object with one of "const", "payload", "value", "expr")", where));
    }
    const auto entry = json.begin();
    const std::string& key = entry.key();
    const Json& value = entry.value();
    const auto kind = std::ranges::find(kBindingKinds, key);
    if (kind == std::end(kBindingKinds))
    {
        return Invalid(std::format("{}: unknown binding kind '{}'", where, key));
    }
    Binding binding;
    binding.kind = static_cast<Binding::Kind>(kind - std::begin(kBindingKinds));
    if (binding.kind == Binding::Kind::Constant)
    {
        binding.text = value.dump();
        return binding;
    }
    if (!value.is_string() || value.get<std::string>().empty())
    {
        return Invalid(std::format("{}: \"{}\" must be a non-empty string", where, key));
    }
    binding.text = value.get<std::string>();
    return binding;
}

Result<ChainNode> ParseNode(const Json& json, std::string_view where, std::vector<std::string>& warnings)
{
    if (!json.is_object())
    {
        return Invalid(std::format("{}: must be an object", where));
    }
    static constexpr std::string_view kKeys[] = {"id", "event", "params"};
    WarnUnknownKeys(json, kKeys, where, warnings);
    ChainNode node;
    INTERI_TRY_ASSIGN(idValue, GetString(json, "id", where, true));
    node.id = std::move(idValue);
    if (!IsName(node.id))
    {
        return Invalid(std::format("{}: id '{}' is not a name ([A-Za-z0-9_])", where, node.id));
    }
    INTERI_TRY_ASSIGN(eventValue, GetString(json, "event", where, true));
    node.event = std::move(eventValue);
    if (node.event.empty())
    {
        return Invalid(std::format("{}: \"event\" is empty", where));
    }
    if (const auto params = json.find("params"); params != json.end())
    {
        if (!params->is_object())
        {
            return Invalid(std::format("{}: \"params\" must be an object", where));
        }
        for (const auto& [name, value] : params->items())
        {
            INTERI_TRY_ASSIGN(binding, ParseBinding(value, std::format("{} param '{}'", where, name)));
            node.params.emplace_back(name, std::move(binding));
        }
    }
    return node;
}

Result<ChainLink> ParseLink(const Json& json, std::string_view where, std::vector<std::string>& warnings)
{
    if (!json.is_object())
    {
        return Invalid(std::format("{}: must be an object", where));
    }
    static constexpr std::string_view kKeys[] = {"from", "to", "condition"};
    WarnUnknownKeys(json, kKeys, where, warnings);
    ChainLink link;
    INTERI_TRY_ASSIGN(from, GetString(json, "from", where, true));
    const std::size_t dot = from.find('.');
    link.fromNode = from.substr(0, dot);
    if (dot != std::string::npos)
    {
        link.fromOutput = from.substr(dot + 1);
        if (!IsName(link.fromOutput))
        {
            return Invalid(std::format("{}: \"from\" '{}' is not '<node>' or '<node>.<output>'", where, from));
        }
    }
    INTERI_TRY_ASSIGN(toNodeValue, GetString(json, "to", where, true));
    link.toNode = std::move(toNodeValue);
    INTERI_TRY_ASSIGN(conditionValue, GetString(json, "condition", where, false));
    link.condition = std::move(conditionValue);
    return link;
}

} // namespace

Result<ChainAsset> ParseChain(std::string_view text)
{
    Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        return Invalid("chain: not a JSON object");
    }
    ChainAsset asset;
    static constexpr std::string_view kKeys[] = {"version", "priority", "nodes", "links"};
    WarnUnknownKeys(json, kKeys, "chain", asset.warnings);
    if (const auto version = json.find("version"); version != json.end())
    {
        if (!version->is_number_unsigned())
        {
            return Invalid("chain: \"version\" must be an unsigned integer");
        }
        if (version->get<std::uint64_t>() > kChainFormatVersion)
        {
            return MakeError(
                ErrorCode::VersionMismatch,
                std::format("chain: version {} is newer than {}", version->get<std::uint64_t>(), kChainFormatVersion));
        }
    }
    else
    {
        asset.warnings.emplace_back("chain: missing \"version\"");
    }
    if (const auto priority = json.find("priority"); priority != json.end())
    {
        if (!priority->is_number_integer() ||
            priority->get<std::int64_t>() < std::numeric_limits<std::int32_t>::min() ||
            priority->get<std::int64_t>() > std::numeric_limits<std::int32_t>::max())
        {
            return Invalid("chain: \"priority\" must be a 32-bit integer");
        }
        asset.priority = priority->get<std::int32_t>();
    }
    if (const auto nodes = json.find("nodes"); nodes != json.end())
    {
        if (!nodes->is_array())
        {
            return Invalid("chain: \"nodes\" must be an array");
        }
        for (std::size_t i = 0; i < nodes->size(); ++i)
        {
            const std::string where = std::format("node {}", i);
            INTERI_TRY_ASSIGN(node, ParseNode((*nodes)[i], where, asset.warnings));
            if (std::ranges::contains(asset.nodes, node.id, &ChainNode::id))
            {
                return Invalid(std::format("{}: duplicate id '{}'", where, node.id));
            }
            asset.nodes.push_back(std::move(node));
        }
    }
    if (const auto links = json.find("links"); links != json.end())
    {
        if (!links->is_array())
        {
            return Invalid("chain: \"links\" must be an array");
        }
        for (std::size_t i = 0; i < links->size(); ++i)
        {
            const std::string where = std::format("link {}", i);
            INTERI_TRY_ASSIGN(link, ParseLink((*links)[i], where, asset.warnings));
            for (const std::string* node : {&link.fromNode, &link.toNode})
            {
                if (!std::ranges::contains(asset.nodes, *node, &ChainNode::id))
                {
                    return Invalid(std::format("{}: no node '{}'", where, *node));
                }
            }
            asset.links.push_back(std::move(link));
        }
    }
    return asset;
}

Result<ChainAsset> LoadChain(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto asset = ParseChain(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!asset)
    {
        return MakeError(asset.error().code, std::format("{}: {}", path, asset.error().message));
    }
    return asset;
}

std::string WriteChain(const ChainAsset& asset)
{
    Json json = Json::object();
    json["version"] = kChainFormatVersion;
    json["priority"] = asset.priority;
    Json nodes = Json::array();
    for (const ChainNode& node : asset.nodes)
    {
        Json entry = {{"id", node.id}, {"event", node.event}};
        if (!node.params.empty())
        {
            Json params = Json::object();
            for (const auto& [name, binding] : node.params)
            {
                const std::string key(kBindingKinds[static_cast<std::size_t>(binding.kind)]);
                params[name] = binding.kind == Binding::Kind::Constant
                                   ? Json{{key, Json::parse(binding.text, nullptr, false)}}
                                   : Json{{key, binding.text}};
            }
            entry["params"] = std::move(params);
        }
        nodes.push_back(std::move(entry));
    }
    json["nodes"] = std::move(nodes);
    Json links = Json::array();
    for (const ChainLink& link : asset.links)
    {
        Json entry = {{"from", link.fromOutput.empty() ? link.fromNode : link.fromNode + "." + link.fromOutput},
                      {"to", link.toNode}};
        if (!link.condition.empty())
        {
            entry["condition"] = link.condition;
        }
        links.push_back(std::move(entry));
    }
    json["links"] = std::move(links);
    return json.dump(2) + "\n";
}

} // namespace interi::chain
