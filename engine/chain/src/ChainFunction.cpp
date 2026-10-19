// SPDX-License-Identifier: MIT
#include <interi/chain/ChainFunction.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <set>

namespace interi::chain
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsName(std::string_view name)
{
    return !name.empty() &&
           std::ranges::all_of(
               name, [](char c)
               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool Scalar(values::ValueType type)
{
    return type != values::ValueType::List;
}

Result<values::ValueType> ParseType(const Json& json, std::string_view where)
{
    const auto type = json.is_string() ? values::ParseValueType(json.get<std::string>()) : std::nullopt;
    if (!type || !Scalar(*type))
    {
        return Invalid(std::format("{}: \"type\" must be a scalar value type", where));
    }
    return *type;
}

EventParam TypedParam(const std::string& name, values::ValueType type)
{
    EventParam param;
    param.name = name;
    param.schema.type = type;
    param.schema.defaultValue = values::ZeroValue(type);
    return param;
}

std::shared_ptr<const EventImpl> Forward(std::size_t count)
{
    return std::make_shared<const EventImpl>(
        [count](EventCall& call)
        {
            std::vector<values::Value> payload;
            for (std::size_t i = 0; i < count; ++i)
            {
                payload.push_back(call.Input(i));
            }
            call.Fire(0, std::move(payload));
        });
}

const ChainFunction* FindFor(const FunctionLookup& lookup, const ChainNode& node)
{
    return lookup(node.event);
}

} // namespace

std::string FunctionCallEvent(std::string_view provider, std::string_view name)
{
    return std::format("{}:{}", provider, name);
}

std::string FunctionEntryEvent(std::string_view provider, std::string_view name)
{
    return FunctionCallEvent(provider, name) + ".Entry";
}

std::string FunctionReturnEvent(std::string_view provider, std::string_view name, std::string_view output)
{
    return std::format("{}.Return.{}", FunctionCallEvent(provider, name), output);
}

Result<void> ValidateChainFunction(const ChainFunction& function)
{
    if (!IsName(function.name))
    {
        return Invalid(std::format("function '{}': not a name ([A-Za-z0-9_])", function.name));
    }
    std::set<std::string> seen;
    for (const ChainFunctionInput& input : function.inputs)
    {
        if (!IsName(input.name) || !seen.insert(input.name).second || !Scalar(input.type))
        {
            return Invalid(std::format("function '{}': input '{}' is invalid or duplicate", function.name, input.name));
        }
    }
    if (function.outputs.empty())
    {
        return Invalid(std::format("function '{}': needs at least one output", function.name));
    }
    seen.clear();
    for (const ChainFunctionOutput& output : function.outputs)
    {
        if (!IsName(output.name) || !seen.insert(output.name).second)
        {
            return Invalid(std::format("function '{}': output '{}' is invalid or duplicate", function.name, output.name));
        }
        std::set<std::string> fields;
        for (const PayloadField& field : output.payload)
        {
            if (!IsName(field.name) || !fields.insert(field.name).second || !Scalar(field.type))
            {
                return Invalid(std::format("function '{}': output '{}' field '{}' is invalid or duplicate",
                                           function.name, output.name, field.name));
            }
        }
    }
    return {};
}

Result<ChainFunction> ParseChainFunction(std::string_view text)
{
    Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        return Invalid("function: not a JSON object");
    }
    ChainFunction function;
    if (const auto name = json.find("name"); name != json.end() && name->is_string())
    {
        function.name = name->get<std::string>();
    }
    else
    {
        return Invalid("function: missing \"name\"");
    }
    if (const auto inputs = json.find("inputs"); inputs != json.end())
    {
        if (!inputs->is_array())
        {
            return Invalid("function: \"inputs\" must be an array");
        }
        for (std::size_t i = 0; i < inputs->size(); ++i)
        {
            const Json& item = (*inputs)[i];
            const std::string where = std::format("input {}", i);
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string() || !item.contains("type"))
            {
                return Invalid(std::format("{}: needs a \"name\" and a \"type\"", where));
            }
            INTERI_TRY_ASSIGN(type, ParseType(item["type"], where));
            function.inputs.push_back({.name = item["name"].get<std::string>(), .type = type});
        }
    }
    if (const auto outputs = json.find("outputs"); outputs != json.end())
    {
        if (!outputs->is_array())
        {
            return Invalid("function: \"outputs\" must be an array");
        }
        for (std::size_t i = 0; i < outputs->size(); ++i)
        {
            const Json& item = (*outputs)[i];
            const std::string where = std::format("output {}", i);
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string())
            {
                return Invalid(std::format("{}: needs a \"name\"", where));
            }
            ChainFunctionOutput output;
            output.name = item["name"].get<std::string>();
            if (const auto payload = item.find("payload"); payload != item.end())
            {
                if (!payload->is_array())
                {
                    return Invalid(std::format("{}: \"payload\" must be an array", where));
                }
                for (const Json& field : *payload)
                {
                    if (!field.is_object() || !field.contains("name") || !field["name"].is_string() ||
                        !field.contains("type"))
                    {
                        return Invalid(std::format("{}: a payload field needs a \"name\" and a \"type\"", where));
                    }
                    INTERI_TRY_ASSIGN(type, ParseType(field["type"], where));
                    output.payload.push_back({.name = field["name"].get<std::string>(), .type = type});
                }
            }
            function.outputs.push_back(std::move(output));
        }
    }
    for (const char* key : {"name", "inputs", "outputs"})
    {
        json.erase(key);
    }
    INTERI_TRY_ASSIGN(body, ParseChain(json.dump()));
    function.body = std::move(body);
    INTERI_TRY(ValidateChainFunction(function));
    return function;
}

Result<ChainFunction> LoadChainFunction(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto function = ParseChainFunction(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!function)
    {
        return MakeError(function.error().code, std::format("{}: {}", path, function.error().message));
    }
    return function;
}

std::string WriteChainFunction(const ChainFunction& function)
{
    Json json = Json::parse(WriteChain(function.body));
    json["name"] = function.name;
    Json inputs = Json::array();
    for (const ChainFunctionInput& input : function.inputs)
    {
        inputs.push_back({{"name", input.name}, {"type", std::string(values::ToString(input.type))}});
    }
    json["inputs"] = std::move(inputs);
    Json outputs = Json::array();
    for (const ChainFunctionOutput& output : function.outputs)
    {
        Json payload = Json::array();
        for (const PayloadField& field : output.payload)
        {
            payload.push_back({{"name", field.name}, {"type", std::string(values::ToString(field.type))}});
        }
        outputs.push_back({{"name", output.name}, {"payload", std::move(payload)}});
    }
    json["outputs"] = std::move(outputs);
    return json.dump(2) + "\n";
}

Result<void> RegisterChainFunction(ChainRegistry& registry, std::string_view provider, const ChainFunction& function)
{
    INTERI_TRY(ValidateChainFunction(function));
    EventDefinition call;
    call.category = "Functions";
    call.tooltip = std::format("Chain function {}", function.name);
    call.implementation = std::make_shared<const EventImpl>([](EventCall&) {});
    EventDefinition entry;
    entry.category = "Functions";
    entry.implementation = Forward(function.inputs.size());
    EventOutput entryOut{.name = "Out", .payload = {}};
    for (const ChainFunctionInput& input : function.inputs)
    {
        call.inputs.push_back(TypedParam(input.name, input.type));
        entry.inputs.push_back(TypedParam(input.name, input.type));
        entryOut.payload.push_back({.name = input.name, .type = input.type});
    }
    entry.outputs.push_back(std::move(entryOut));
    for (const ChainFunctionOutput& output : function.outputs)
    {
        call.outputs.push_back({.name = output.name, .payload = output.payload});
    }
    INTERI_TRY(registry.Events().Provide(provider, FunctionCallEvent(provider, function.name), std::move(call)));
    INTERI_TRY(registry.Events().Provide(provider, FunctionEntryEvent(provider, function.name), std::move(entry)));
    for (const ChainFunctionOutput& output : function.outputs)
    {
        EventDefinition exit;
        exit.category = "Functions";
        exit.implementation = Forward(output.payload.size());
        for (const PayloadField& field : output.payload)
        {
            exit.inputs.push_back(TypedParam(field.name, field.type));
        }
        exit.outputs.push_back({.name = "Out", .payload = output.payload});
        INTERI_TRY(registry.Events().Provide(provider, FunctionReturnEvent(provider, function.name, output.name),
                                             std::move(exit)));
    }
    return {};
}

Result<ChainAsset> ExpandChainFunctions(const ChainAsset& asset, std::string_view provider,
                                        const FunctionLookup& lookup)
{
    ChainAsset result = asset;
    for (int depth = 0; depth <= kMaxFunctionDepth; ++depth)
    {
        const auto call = std::ranges::find_if(result.nodes, [&](const ChainNode& node)
                                               { return FindFor(lookup, node) != nullptr; });
        if (call == result.nodes.end())
        {
            return result;
        }
        if (depth == kMaxFunctionDepth)
        {
            return Invalid("chain functions nest too deeply (recursion?)");
        }
        // Inline every call of this round, then look again (bodies may call functions).
        std::vector<ChainNode> nodes;
        std::vector<ChainLink> links = result.links;
        for (const ChainNode& node : result.nodes)
        {
            const ChainFunction* function = FindFor(lookup, node);
            if (function == nullptr)
            {
                nodes.push_back(node);
                continue;
            }
            const std::string prefix = node.id + "__";
            const std::string entryEvent = FunctionEntryEvent(provider, function->name);
            std::string entryId;
            std::vector<std::pair<std::string, std::string>> returns; // output name -> node id
            for (const ChainNode& body : function->body.nodes)
            {
                ChainNode copy = body;
                copy.id = prefix + body.id;
                if (body.event == entryEvent)
                {
                    if (!entryId.empty())
                    {
                        return Invalid(std::format("function '{}': more than one Entry node", function->name));
                    }
                    entryId = copy.id;
                    copy.params = node.params;
                }
                for (const ChainFunctionOutput& output : function->outputs)
                {
                    if (body.event == FunctionReturnEvent(provider, function->name, output.name))
                    {
                        returns.emplace_back(output.name, copy.id);
                    }
                }
                copy.pos = node.pos;
                nodes.push_back(std::move(copy));
            }
            if (entryId.empty())
            {
                return Invalid(std::format("function '{}': the body has no Entry node ({})", function->name,
                                           entryEvent));
            }
            for (ChainLink link : function->body.links)
            {
                link.fromNode = prefix + link.fromNode;
                link.toNode = prefix + link.toNode;
                links.push_back(std::move(link));
            }
            const std::string firstOutput = function->outputs.front().name;
            std::vector<ChainLink> rewritten;
            for (ChainLink& link : links)
            {
                if (link.toNode == node.id)
                {
                    link.toNode = entryId;
                }
                if (link.fromNode != node.id)
                {
                    rewritten.push_back(std::move(link));
                    continue;
                }
                const std::string output = link.fromOutput.empty() ? firstOutput : link.fromOutput;
                for (const auto& [name, id] : returns)
                {
                    if (name == output)
                    {
                        ChainLink exit = link;
                        exit.fromNode = id;
                        exit.fromOutput = "Out";
                        rewritten.push_back(std::move(exit));
                    }
                }
            }
            links = std::move(rewritten);
        }
        result.nodes = std::move(nodes);
        result.links = std::move(links);
    }
    return result;
}

} // namespace interi::chain
