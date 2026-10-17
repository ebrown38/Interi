// SPDX-License-Identifier: MIT
#pragma once

// Shared JSON parsing for the data sections: value schemas and item ids.

#include <interi/values/Value.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::values::detail
{

[[nodiscard]] std::unexpected<Error> Invalid(std::string message);

/// The keys ParseSchema reads.
inline constexpr std::string_view kSchemaKeys[] = {"type", "element", "options",  "default", "min",     "max",
                                                   "step", "unit",    "category", "tooltip", "readOnly"};

/// Reads a schema from an object's schema keys (`where` prefixes messages) and validates it.
[[nodiscard]] Result<ValueSchema> ParseSchema(const nlohmann::json& object, std::string_view where);

/// Writes a schema's keys into `object` (the inverse of ParseSchema; empty metadata is left out).
void WriteSchema(const ValueSchema& schema, nlohmann::json& object);

/// Adds a warning for every key of `object` that is in neither list.
void WarnUnknownKeys(const nlohmann::json& object, std::span<const std::string_view> known,
                     std::span<const std::string_view> alsoKnown, std::string_view where,
                     std::vector<std::string>& warnings);

/// Reads an optional unsigned integer key (default `fallback`).
[[nodiscard]] Result<std::uint32_t> GetU32(const nlohmann::json& object, std::string_view key, std::uint32_t fallback,
                                           std::string_view where);

/// Reads an optional boolean key.
[[nodiscard]] Result<bool> GetBool(const nlohmann::json& object, std::string_view key, bool fallback,
                                   std::string_view where);

/// Reads a required string key.
[[nodiscard]] Result<std::string> GetString(const nlohmann::json& object, std::string_view key, std::string_view where);

/// Resolves a declaration's id. `"id": <name>` is a local name ("<module>:<name>"); with `"shared": true` it is a
/// shared contract id; `"overrides": <local id>` names another module's local item. Sets `isOverride`.
[[nodiscard]] Result<std::string> ParseItemId(const nlohmann::json& object, std::string_view module,
                                              std::string_view where, bool& isOverride);

/// The keys ParseItemId reads.
inline constexpr std::string_view kItemIdKeys[] = {"id", "shared", "overrides", "version"};

/// Reads the whole file at `path`. IoError if it can't.
[[nodiscard]] Result<std::string> ReadTextFile(const std::string& path);

} // namespace interi::values::detail
