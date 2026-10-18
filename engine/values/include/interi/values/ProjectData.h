// SPDX-License-Identifier: MIT
#pragma once

// The project's own data, saved two ways:
//
// JSON, in the project (sorted keys, one entry per line, so it diffs well):
//
//     {
//       "format": 1,
//       "values":     { "com.author.fps:JumpHeight": 2.5 },
//       "characters": { "com.author.fps:Soldier": { "attr.Health": 175 } },
//       "tables":     { "com.author.fps:Guns": { "shotgun": { "range": 20, "mode": "Auto" } } },
//       "schemas":    { "values": { "com.author.fps:JumpHeight": 1 }, "characters": { "com.author.fps:Soldier": 1 },
//                       "tables": { "com.author.fps:Guns": 2 } }
//     }
//
// "schemas" records, per section, the version of every value, archetype and table definition the data was written
// against (per section: one id may name items of several kinds). On
// load, data written against the current version must fit exactly (anything else is an error: the file was edited
// by hand); data written against another version migrates with a warning each: numbers convert between bool, int,
// u32 and float and clamp into the new range, a renamed column ("renamedFrom") takes its old cells, and what can't
// be converted (a removed column, an unknown enum option, another type) falls back to the default. Entries for ids no
// loaded module defines are kept as they are and written back.
//
// Cooked binary ("IVAL", payload hash + versioned Archive, like the other cooked formats): the resolved data a
// shipped game reads without module.json: every Value's effective value, every archetype's layout, every table's
// columns and effective rows.

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/values/DataRegistry.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::values
{

inline constexpr std::uint32_t kProjectDataFormat = 1;

/// Loads the project's data from JSON into `data` (replacing its project layer). All or nothing: InvalidArgument for
/// malformed JSON or data that doesn't fit a current-version schema, with the item in the message. Returns the
/// migration warnings.
[[nodiscard]] Result<std::vector<std::string>> LoadProjectData(std::string_view json, DataRegistry& data);

/// The project layer of `data` as JSON (see the top of this header).
[[nodiscard]] std::string WriteProjectData(const DataRegistry& data);

inline constexpr FourCC kCookedDataMagic = MakeFourCC("IVAL");
inline constexpr std::uint32_t kCookedDataVersion = 1;

struct CookedValue
{
    std::string id;
    Value value;

    friend bool operator==(const CookedValue&, const CookedValue&) = default;
};

struct CookedAttribute
{
    std::string id;
    ValueType type = ValueType::Float;
    Value defaultValue;

    friend bool operator==(const CookedAttribute&, const CookedAttribute&) = default;
};

struct CookedArchetype
{
    std::string id;
    std::uint64_t layoutVersion = 0;
    std::vector<CookedAttribute> attributes;

    friend bool operator==(const CookedArchetype&, const CookedArchetype&) = default;
};

struct CookedColumn
{
    std::string name;
    ValueType type = ValueType::Float;

    friend bool operator==(const CookedColumn&, const CookedColumn&) = default;
};

struct CookedRow
{
    std::string id;
    std::vector<Value> cells; ///< One per column, in column order.

    friend bool operator==(const CookedRow&, const CookedRow&) = default;
};

struct CookedTable
{
    std::string id;
    std::vector<CookedColumn> columns;
    std::vector<CookedRow> rows; ///< By row id.

    friend bool operator==(const CookedTable&, const CookedTable&) = default;
};

/// The resolved data of a project, as a shipped game reads it. Everything is sorted by id.
struct CookedData
{
    std::vector<CookedValue> values;
    std::vector<CookedArchetype> archetypes;
    std::vector<CookedTable> tables;

    /// The value with this id, or nullptr.
    [[nodiscard]] const Value* FindValue(std::string_view id) const noexcept;
    [[nodiscard]] const CookedTable* FindTable(std::string_view id) const noexcept;

    friend bool operator==(const CookedData&, const CookedData&) = default;
};

/// Resolves everything `data` holds. Archetypes whose layout doesn't resolve are left out, with a warning.
[[nodiscard]] CookedData CookData(const DataRegistry& data, std::vector<std::string>* warnings = nullptr);

[[nodiscard]] std::vector<std::byte> WriteCookedData(const CookedData& cooked);
/// Corrupt for a damaged file (payload hash), VersionMismatch for a newer format.
[[nodiscard]] Result<CookedData> ReadCookedData(std::span<const std::byte> bytes);

} // namespace interi::values
