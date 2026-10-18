// SPDX-License-Identifier: MIT
#include <interi/chain/Compiler.h>
#include <interi/chain/CoreLibrary.h>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <utility>

namespace interi::chain
{

using values::Value;
using values::ValueType;

std::string Diagnostic::Text() const
{
    std::string text = severity == Severity::Error ? "error:" : "warning:";
    if (!node.empty())
    {
        text += std::format(" node '{}'", node);
    }
    if (link)
    {
        text += std::format(" link {}", *link);
    }
    if (!input.empty())
    {
        text += std::format(" {}", input);
    }
    if (span)
    {
        text += std::format(" [{}, {})", span->begin, span->end);
    }
    return text + ": " + message;
}

std::size_t ChainProgram::NodeIndex(std::string_view id) const noexcept
{
    return static_cast<std::size_t>(std::ranges::find(nodes, id, &ProgramNode::id) - nodes.begin());
}

namespace
{

class Compiler
{
public:
    Compiler(const ChainAsset& asset, const ChainRegistry& registry, const values::ValueRegistry* values)
        : m_asset(asset)
        , m_registry(registry)
        , m_values(values)
    {
    }

    CompileResult Run()
    {
        ResolveEvents();
        if (HasErrors())
        {
            return Finish();
        }
        MarkReachable();
        BuildNodes();
        BuildLinks();
        return Finish();
    }

private:
    struct Resolved
    {
        const EventDefinition* definition = nullptr;
        bool reachable = false;
        std::uint32_t programIndex = 0;
    };

    void Error(std::string node, std::optional<std::size_t> link, std::string input, std::string message,
               std::optional<SourceSpan> span = std::nullopt)
    {
        m_result.diagnostics.push_back({.severity = Diagnostic::Severity::Error,
                                        .node = std::move(node),
                                        .link = link,
                                        .input = std::move(input),
                                        .message = std::move(message),
                                        .span = span});
    }

    [[nodiscard]] bool HasErrors() const
    {
        return std::ranges::any_of(m_result.diagnostics,
                                   [](const Diagnostic& d) { return d.severity == Diagnostic::Severity::Error; });
    }

    CompileResult Finish()
    {
        if (HasErrors())
        {
            m_result.program.reset();
        }
        else
        {
            m_result.program = std::make_shared<const ChainProgram>(std::move(m_program));
        }
        return std::move(m_result);
    }

    [[nodiscard]] std::size_t NodeIndex(std::string_view id) const
    {
        return static_cast<std::size_t>(std::ranges::find(m_asset.nodes, id, &ChainNode::id) - m_asset.nodes.begin());
    }

    void ResolveEvents()
    {
        m_resolved.resize(m_asset.nodes.size());
        for (std::size_t i = 0; i < m_asset.nodes.size(); ++i)
        {
            const ChainNode& node = m_asset.nodes[i];
            const EventDefinition* definition = m_registry.Events().Resolve(node.event);
            if (definition == nullptr)
            {
                Error(node.id, std::nullopt, "", std::format("no event '{}'", node.event));
                continue;
            }
            m_resolved[i].definition = definition;
            for (const auto& [name, binding] : node.params)
            {
                if (definition->InputIndex(name) == definition->inputs.size())
                {
                    Error(node.id, std::nullopt, name, std::format("event '{}' has no input '{}'", node.event, name));
                }
            }
        }
        for (std::size_t l = 0; l < m_asset.links.size(); ++l)
        {
            const ChainLink& link = m_asset.links[l];
            const Resolved& from = m_resolved[NodeIndex(link.fromNode)];
            const Resolved& to = m_resolved[NodeIndex(link.toNode)];
            if (from.definition != nullptr && SourceOutput(link, *from.definition) == nullptr)
            {
                Error(link.fromNode, l, "",
                      link.fromOutput.empty() ? std::string("the event has no outputs")
                                              : std::format("the event has no output '{}'", link.fromOutput));
            }
            if (to.definition != nullptr && to.definition->kind == EventKind::Trigger)
            {
                Error(link.toNode, l, "", "a link can't lead to a trigger");
            }
        }
    }

    [[nodiscard]] static const EventOutput* SourceOutput(const ChainLink& link, const EventDefinition& definition)
    {
        if (link.fromOutput.empty())
        {
            return definition.outputs.empty() ? nullptr : &definition.outputs.front();
        }
        return definition.FindOutput(link.fromOutput);
    }

    void MarkReachable()
    {
        std::vector<std::size_t> pending;
        for (std::size_t i = 0; i < m_asset.nodes.size(); ++i)
        {
            if (m_resolved[i].definition->kind == EventKind::Trigger)
            {
                m_resolved[i].reachable = true;
                pending.push_back(i);
            }
        }
        while (!pending.empty())
        {
            const std::string& id = m_asset.nodes[pending.back()].id;
            pending.pop_back();
            for (const ChainLink& link : m_asset.links)
            {
                if (link.fromNode != id)
                {
                    continue;
                }
                const std::size_t target = NodeIndex(link.toNode);
                if (!m_resolved[target].reachable)
                {
                    m_resolved[target].reachable = true;
                    pending.push_back(target);
                }
            }
        }
        for (std::size_t i = 0; i < m_asset.nodes.size(); ++i)
        {
            if (!m_resolved[i].reachable)
            {
                m_result.diagnostics.push_back({.severity = Diagnostic::Severity::Warning,
                                                .node = m_asset.nodes[i].id,
                                                .link = std::nullopt,
                                                .input = "",
                                                .message = "unreachable: no trigger leads here; left out",
                                                .span = std::nullopt});
            }
        }
    }

    [[nodiscard]] const Binding* FindBinding(const ChainNode& node, std::string_view input) const
    {
        const auto it = std::ranges::find(node.params, input, &std::pair<std::string, Binding>::first);
        return it == node.params.end() ? nullptr : &it->second;
    }

    /// The schema of input `index` of `node`: its own, or (typeFrom) the named Value's. nullptr after an error.
    const values::ValueSchema* InputSchema(const ChainNode& node, const EventDefinition& definition, std::size_t index,
                                           std::optional<std::size_t> link)
    {
        const EventParam& input = definition.inputs[index];
        if (input.typeFrom.empty())
        {
            return &input.schema;
        }
        const Binding* source = FindBinding(node, input.typeFrom);
        if (source == nullptr || source->kind != Binding::Kind::Constant)
        {
            Error(node.id, link, input.name,
                  std::format("input '{}' takes its type from the Value named by '{}', which must be a constant",
                              input.name, input.typeFrom));
            return nullptr;
        }
        values::ValueSchema stringSchema;
        stringSchema.type = ValueType::String;
        auto id = values::ParseValueJson(source->text, stringSchema);
        const values::ValueDefinition* value =
            id && m_values != nullptr ? m_values->Definition(std::get<std::string>(*id)) : nullptr;
        if (value == nullptr)
        {
            Error(node.id, link, input.typeFrom,
                  std::format("'{}' names no Value", id ? std::get<std::string>(*id) : source->text));
            return nullptr;
        }
        return &value->schema;
    }

    /// Compiles one binding against `payload`; an unbound input is its default.
    std::optional<CompiledExpression> CompileInput(const ChainNode& node, const EventDefinition& definition,
                                                   std::size_t index, std::span<const PayloadField> payload,
                                                   std::optional<std::size_t> link)
    {
        const EventParam& input = definition.inputs[index];
        const values::ValueSchema* schema = InputSchema(node, definition, index, link);
        if (schema == nullptr)
        {
            return std::nullopt;
        }
        const Binding* binding = FindBinding(node, input.name);
        if (binding == nullptr)
        {
            if (!input.typeFrom.empty())
            {
                Error(node.id, link, input.name, "must be bound");
                return std::nullopt;
            }
            return ConstantExpression(schema->defaultValue);
        }
        if (binding->kind == Binding::Kind::Constant)
        {
            auto value = values::ParseValueJson(binding->text, *schema);
            if (value)
            {
                if (auto valid = values::ValidateValue(*schema, *value); !valid)
                {
                    value = std::unexpected(valid.error());
                }
            }
            if (!value)
            {
                Error(node.id, link, input.name, value.error().message);
                return std::nullopt;
            }
            return ConstantExpression(std::move(*value));
        }
        std::string source;
        switch (binding->kind)
        {
        case Binding::Kind::Payload:
            source = "payload." + binding->text;
            break;
        case Binding::Kind::Value:
            source = "value(\"" + binding->text + "\")";
            break;
        default:
            source = binding->text;
            break;
        }
        const ExprEnvironment environment{.payload = payload, .values = m_values, .functions = &m_registry.Functions()};
        auto compiled = CompileExpression(source, environment);
        if (!compiled)
        {
            // Spans only mean something in an expression the author wrote.
            Error(node.id, link, input.name, compiled.error().message,
                  binding->kind == Binding::Kind::Expression ? std::optional(compiled.error().span) : std::nullopt);
            return std::nullopt;
        }
        if (compiled->Type() != schema->type)
        {
            const ValueType type = compiled->Type();
            const bool promotable =
                schema->type == ValueType::Float && (type == ValueType::Int || type == ValueType::U32);
            if (promotable)
            {
                compiled = CompileExpression("float(" + source + ")", environment);
            }
            else
            {
                Error(node.id, link, input.name,
                      std::format("is {}, but the binding is {}", values::ToString(schema->type),
                                  values::ToString(type)));
                return std::nullopt;
            }
        }
        return std::move(*compiled);
    }

    void BuildNodes()
    {
        m_program.priority = m_asset.priority;
        for (std::size_t i = 0; i < m_asset.nodes.size(); ++i)
        {
            Resolved& resolved = m_resolved[i];
            if (!resolved.reachable)
            {
                continue;
            }
            const ChainNode& node = m_asset.nodes[i];
            const EventDefinition& definition = *resolved.definition;
            ProgramNode compiled;
            compiled.id = node.id;
            compiled.event = node.event;
            compiled.kind = definition.kind;
            compiled.authority = definition.authority;
            compiled.implementation = definition.implementation;
            compiled.outputLinks.resize(definition.outputs.size());
            for (const EventOutput& output : definition.outputs)
            {
                compiled.outputs.push_back(output.name);
            }
            compiled.keyInput = definition.InputIndex(kTriggerKeyInput);
            if (definition.kind == EventKind::Trigger)
            {
                for (std::size_t input = 0; input < definition.inputs.size(); ++input)
                {
                    const Binding* binding = FindBinding(node, definition.inputs[input].name);
                    if (binding != nullptr && binding->kind != Binding::Kind::Constant)
                    {
                        Error(node.id, std::nullopt, definition.inputs[input].name, "trigger inputs must be constants");
                        continue;
                    }
                    auto value = CompileInput(node, definition, input, {}, std::nullopt);
                    compiled.triggerInputs.push_back(value ? EvaluateExpression(*value, {}) : Value{});
                }
            }
            resolved.programIndex = static_cast<std::uint32_t>(m_program.nodes.size());
            m_program.nodes.push_back(std::move(compiled));
        }
    }

    void BuildLinks()
    {
        // Links grouped by (source node, output), in asset order within a group.
        for (std::size_t n = 0; n < m_asset.nodes.size(); ++n)
        {
            if (!m_resolved[n].reachable)
            {
                continue;
            }
            const ChainNode& sourceNode = m_asset.nodes[n];
            const EventDefinition& source = *m_resolved[n].definition;
            ProgramNode& programNode = m_program.nodes[m_resolved[n].programIndex];
            for (std::size_t output = 0; output < source.outputs.size(); ++output)
            {
                const auto first = static_cast<std::uint32_t>(m_program.links.size());
                for (std::size_t l = 0; l < m_asset.links.size(); ++l)
                {
                    const ChainLink& link = m_asset.links[l];
                    if (link.fromNode != sourceNode.id || SourceOutput(link, source) != &source.outputs[output])
                    {
                        continue;
                    }
                    BuildLink(l, source.outputs[output].payload);
                }
                programNode.outputLinks[output] = {first, static_cast<std::uint32_t>(m_program.links.size())};
            }
        }
    }

    void BuildLink(std::size_t index, std::span<const PayloadField> payload)
    {
        const ChainLink& link = m_asset.links[index];
        const std::size_t targetIndex = NodeIndex(link.toNode);
        const ChainNode& target = m_asset.nodes[targetIndex];
        const EventDefinition& definition = *m_resolved[targetIndex].definition;
        ProgramLink compiled;
        compiled.target = m_resolved[targetIndex].programIndex;
        compiled.assetLink = index;
        if (!link.condition.empty())
        {
            const ExprEnvironment environment{
                .payload = payload, .values = m_values, .functions = &m_registry.Functions()};
            auto condition = CompileExpression(link.condition, environment);
            if (!condition)
            {
                Error(link.fromNode, index, "condition", condition.error().message, condition.error().span);
            }
            else if (condition->Type() != ValueType::Bool)
            {
                Error(link.fromNode, index, "condition",
                      std::format("must be bool, not {}", values::ToString(condition->Type())),
                      SourceSpan{.begin = 0, .end = static_cast<std::uint32_t>(link.condition.size())});
            }
            else
            {
                compiled.condition = std::move(*condition);
            }
        }
        for (std::size_t input = 0; input < definition.inputs.size(); ++input)
        {
            auto value = CompileInput(target, definition, input, payload, index);
            compiled.inputs.push_back(value ? std::move(*value) : CompiledExpression{});
        }
        m_program.links.push_back(std::move(compiled));
    }

    const ChainAsset& m_asset;
    const ChainRegistry& m_registry;
    const values::ValueRegistry* m_values;
    std::vector<Resolved> m_resolved;
    ChainProgram m_program;
    CompileResult m_result;
};

} // namespace

CompileResult CompileChain(const ChainAsset& asset, const ChainRegistry& registry, const values::ValueRegistry* values)
{
    return Compiler(asset, registry, values).Run();
}

} // namespace interi::chain
