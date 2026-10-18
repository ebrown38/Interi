// SPDX-License-Identifier: MIT
#include <interi/chain/Functions.h>

#include <format>
#include <ranges>

namespace interi::chain
{

namespace
{

Result<void> CheckDefinition(std::string_view id, const FunctionDefinition& definition)
{
    if (definition.implementation == nullptr || !*definition.implementation)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("function '{}' has no implementation", id));
    }
    if (definition.result == values::ValueType::List ||
        std::ranges::contains(definition.parameters, values::ValueType::List))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("function '{}' can't take or return a list", id));
    }
    return {};
}

} // namespace

Result<void> FunctionRegistry::Provide(std::string_view provider, std::string_view id, FunctionDefinition definition)
{
    INTERI_TRY(CheckDefinition(id, definition));
    return m_registry.Provide(provider, id, std::move(definition));
}

Result<void> FunctionRegistry::Override(std::string_view provider, std::string_view targetId,
                                        FunctionDefinition definition)
{
    INTERI_TRY(CheckDefinition(targetId, definition));
    if (const FunctionDefinition* existing = m_registry.Resolve(targetId);
        existing != nullptr && (existing->parameters != definition.parameters || existing->result != definition.result))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' overrides function '{}' with a different signature", provider, targetId));
    }
    return m_registry.Override(provider, targetId, std::move(definition));
}

} // namespace interi::chain
