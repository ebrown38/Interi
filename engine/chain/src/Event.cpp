// SPDX-License-Identifier: MIT
#include <interi/chain/Event.h>
#include <interi/core/Assert.h>

#include <algorithm>
#include <format>
#include <ranges>

namespace interi::chain
{

namespace
{

[[nodiscard]] bool IsName(std::string_view name)
{
    return !name.empty() &&
           std::ranges::all_of(
               name, [](char c)
               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; });
}

Result<void> CheckNames(std::string_view id, std::string_view what, std::span<const std::string_view> names)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (!IsName(names[i]))
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("event '{}': {} '{}' is not a name ([A-Za-z0-9_])", id, what, names[i]));
        }
        if (std::ranges::find(names.subspan(0, i), names[i]) != names.begin() + static_cast<std::ptrdiff_t>(i))
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("event '{}': duplicate {} '{}'", id, what, names[i]));
        }
    }
    return {};
}

Result<void> CheckDefinition(std::string_view id, const EventDefinition& definition)
{
    const bool hasImplementation = definition.implementation != nullptr && *definition.implementation;
    if (definition.kind == EventKind::Action && !hasImplementation)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("action event '{}' has no implementation", id));
    }
    if (definition.kind == EventKind::Trigger && (definition.implementation != nullptr || definition.latent))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("trigger event '{}' can't have an implementation or be latent", id));
    }
    std::vector<std::string_view> names;
    names.reserve(definition.inputs.size());
    for (const EventParam& input : definition.inputs)
    {
        names.push_back(input.name);
    }
    INTERI_TRY(CheckNames(id, "input", names));
    for (const EventParam& input : definition.inputs)
    {
        if (!input.typeFrom.empty())
        {
            const std::size_t source = definition.InputIndex(input.typeFrom);
            if (source == definition.inputs.size() || !definition.inputs[source].typeFrom.empty() ||
                definition.inputs[source].schema.type != values::ValueType::String)
            {
                return MakeError(ErrorCode::InvalidArgument,
                                 std::format("event '{}': input '{}' takes its type from '{}', which is not a string "
                                             "input",
                                             id, input.name, input.typeFrom));
            }
            continue;
        }
        if (auto valid = values::ValidateSchema(input.schema); !valid)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("event '{}': input '{}': {}", id, input.name, valid.error().message));
        }
    }
    names.clear();
    for (const EventOutput& output : definition.outputs)
    {
        names.push_back(output.name);
    }
    INTERI_TRY(CheckNames(id, "output", names));
    for (const EventOutput& output : definition.outputs)
    {
        names.clear();
        for (const PayloadField& field : output.payload)
        {
            names.push_back(field.name);
        }
        INTERI_TRY(CheckNames(id, "payload field", names));
    }
    return {};
}

[[nodiscard]] bool SameSignature(const EventDefinition& a, const EventDefinition& b)
{
    const auto sameInput = [](const EventParam& x, const EventParam& y)
    {
        return x.name == y.name && x.typeFrom == y.typeFrom &&
               (!x.typeFrom.empty() || (x.schema.type == y.schema.type && x.schema.element == y.schema.element));
    };
    const auto sameField = [](const PayloadField& x, const PayloadField& y)
    { return x.name == y.name && x.type == y.type; };
    const auto sameOutput = [&](const EventOutput& x, const EventOutput& y)
    { return x.name == y.name && std::ranges::equal(x.payload, y.payload, sameField); };
    return a.kind == b.kind && std::ranges::equal(a.inputs, b.inputs, sameInput) &&
           std::ranges::equal(a.outputs, b.outputs, sameOutput);
}

} // namespace

const EventOutput* EventDefinition::FindOutput(std::string_view name) const noexcept
{
    const auto it = std::ranges::find(outputs, name, &EventOutput::name);
    return it == outputs.end() ? nullptr : &*it;
}

std::size_t EventDefinition::InputIndex(std::string_view name) const noexcept
{
    return static_cast<std::size_t>(std::ranges::find(inputs, name, &EventParam::name) - inputs.begin());
}

void EventCall::Fire(std::size_t output, std::vector<values::Value> payload)
{
    INTERI_ASSERT(output < m_outputCount, "firing an output the event doesn't have");
    m_fired.push_back({.output = output, .delayNanoseconds = -1, .payload = std::move(payload)});
}

void EventCall::FireAfter(std::int64_t nanoseconds, std::size_t output, std::vector<values::Value> payload)
{
    INTERI_ASSERT(output < m_outputCount, "firing an output the event doesn't have");
    m_fired.push_back(
        {.output = output, .delayNanoseconds = std::max<std::int64_t>(nanoseconds, 0), .payload = std::move(payload)});
}

void EventCall::Defer(Effect effect)
{
    m_effects.push_back(std::move(effect));
}

Result<void> EventRegistry::Provide(std::string_view provider, std::string_view id, EventDefinition definition)
{
    INTERI_TRY(CheckDefinition(id, definition));
    return m_registry.Provide(provider, id, std::move(definition));
}

Result<void> EventRegistry::Override(std::string_view provider, std::string_view targetId, EventDefinition definition)
{
    INTERI_TRY(CheckDefinition(targetId, definition));
    if (const EventDefinition* existing = m_registry.Resolve(targetId);
        existing != nullptr && !SameSignature(*existing, definition))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' overrides event '{}' with different inputs or outputs", provider, targetId));
    }
    return m_registry.Override(provider, targetId, std::move(definition));
}

ChainRegistry::Extracted ChainRegistry::ExtractProvider(std::string_view provider)
{
    return {.events = m_events.Registry().ExtractProvider(provider),
            .functions = m_functions.Registry().ExtractProvider(provider)};
}

void ChainRegistry::Restore(Extracted extracted)
{
    m_events.Registry().Restore(std::move(extracted.events));
    m_functions.Registry().Restore(std::move(extracted.functions));
}

std::vector<sdk::Subscription> ChainRegistry::Subscribe(const std::function<void(std::string_view id)>& callback)
{
    std::vector<sdk::Subscription> subscriptions;
    subscriptions.push_back(m_events.Registry().Subscribe(callback));
    subscriptions.push_back(m_functions.Registry().Subscribe(callback));
    return subscriptions;
}

} // namespace interi::chain
