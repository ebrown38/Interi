// SPDX-License-Identifier: MIT
#pragma once

// nlohmann::json conversions for values (private: nlohmann is not part of the public interface).

#include <interi/values/Value.h>

#include <nlohmann/json.hpp>

#include <string_view>

namespace interi::values::detail
{

/// Decodes `json` as a value of `type` (`element` for lists). `where` prefixes error messages.
[[nodiscard]] Result<Value> ValueFromJson(const nlohmann::json& json, ValueType type, ValueType element,
                                          std::string_view where);

[[nodiscard]] inline Result<Value> ValueFromJson(const nlohmann::json& json, const ValueSchema& schema,
                                                 std::string_view where)
{
    return ValueFromJson(json, schema.type, schema.element, where);
}

[[nodiscard]] nlohmann::json ValueToJson(const Value& value);

} // namespace interi::values::detail
