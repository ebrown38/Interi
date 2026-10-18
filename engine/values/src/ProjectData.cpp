// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/sdk/Manifest.h>
#include <interi/values/ProjectData.h>

#include "ValueJson.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <tuple>

namespace interi::values
{

namespace
{

constexpr FourCC kCookedPayloadMagic = MakeFourCC("IVAP");

/// The sections of a project data file; "schemas" has one object of versions per section, since one id may name
/// items of several kinds (a shared attribute and a shared value, a value and a table of one module).
constexpr std::string_view kSections[] = {"values", "characters", "tables"};

/// The key of an item in DataRegistry::LoadedSchemaVersions: "<section>/<id>".
std::string SchemaKey(std::string_view section, std::string_view id)
{
    return std::format("{}/{}", section, id);
}

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

[[nodiscard]] bool IsNumeric(ValueType type) noexcept
{
    return type == ValueType::Bool || type == ValueType::Int || type == ValueType::U32 || type == ValueType::Float;
}

[[nodiscard]] double Clamped(double value, const ValueMeta& meta) noexcept
{
    if (meta.min)
    {
        value = std::max(value, *meta.min);
    }
    if (meta.max)
    {
        value = std::min(value, *meta.max);
    }
    return value;
}

/// A number as a value of a numeric schema, clamped into its range.
[[nodiscard]] Value FromNumber(double number, const ValueSchema& schema)
{
    number = Clamped(std::isfinite(number) ? number : 0.0, schema.meta);
    switch (schema.type)
    {
    case ValueType::Bool:
        return number != 0.0;
    case ValueType::Int:
        return static_cast<std::int32_t>(std::clamp(std::trunc(number), -2147483648.0, 2147483647.0));
    case ValueType::U32:
        return static_cast<std::uint32_t>(std::clamp(std::trunc(number), 0.0, 4294967295.0));
    default:
        return static_cast<float>(number);
    }
}

/// Clamps the numbers of a decoded value into the schema's range.
[[nodiscard]] Value Clamp(const Value& value, const ValueSchema& schema)
{
    const auto clampFloat = [&](float f)
    { return static_cast<float>(Clamped(std::isfinite(f) ? static_cast<double>(f) : 0.0, schema.meta)); };
    if (const auto* integer = std::get_if<std::int32_t>(&value.Variant()))
    {
        return FromNumber(static_cast<double>(*integer), schema);
    }
    if (const auto* unsignedInteger = std::get_if<std::uint32_t>(&value.Variant()))
    {
        return FromNumber(static_cast<double>(*unsignedInteger), schema);
    }
    if (const auto* real = std::get_if<float>(&value.Variant()))
    {
        return FromNumber(static_cast<double>(*real), schema);
    }
    if (const auto* v = std::get_if<Vec3>(&value.Variant()))
    {
        return Vec3(clampFloat(v->x), clampFloat(v->y), clampFloat(v->z));
    }
    if (const auto* c = std::get_if<Color>(&value.Variant()))
    {
        return Color{.r = clampFloat(c->r), .g = clampFloat(c->g), .b = clampFloat(c->b), .a = clampFloat(c->a)};
    }
    if (const auto* list = std::get_if<ValueList>(&value.Variant()))
    {
        ValueSchema element = schema;
        element.type = schema.element;
        ValueList clamped{.element = list->element, .items = {}};
        for (const Value& item : list->items)
        {
            clamped.items.push_back(Clamp(item, element));
        }
        return clamped;
    }
    return value;
}

/// Reads one stored value. Written against the current schema version it must fit exactly; otherwise it migrates
/// (see ProjectData.h), adding a warning.
Result<Value> ReadStored(const nlohmann::json& json, const ValueSchema& schema, bool sameVersion,
                         const std::string& where, std::vector<std::string>& warnings)
{
    Result<Value> decoded = detail::ValueFromJson(json, schema, where);
    if (decoded)
    {
        const Result<void> valid = ValidateValue(schema, *decoded);
        if (valid)
        {
            return decoded;
        }
        if (sameVersion)
        {
            return Invalid(std::format("{}: {}", where, valid.error().message));
        }
        Value clamped = Clamp(*decoded, schema);
        if (ValidateValue(schema, clamped))
        {
            warnings.push_back(std::format("{}: {} migrated to {}", where, json.dump(), FormatValue(clamped)));
            return clamped;
        }
    }
    else if (sameVersion)
    {
        return std::unexpected(std::move(decoded.error()));
    }
    else if ((json.is_number() || json.is_boolean()) && IsNumeric(schema.type))
    {
        Value converted = FromNumber(json.is_boolean() ? (json.get<bool>() ? 1.0 : 0.0) : json.get<double>(), schema);
        if (ValidateValue(schema, converted))
        {
            warnings.push_back(std::format("{}: {} migrated to {}", where, json.dump(), FormatValue(converted)));
            return converted;
        }
    }
    warnings.push_back(std::format("{}: {} doesn't fit the new definition; using the default {}", where, json.dump(),
                                   FormatValue(schema.defaultValue)));
    return schema.defaultValue;
}

Result<const nlohmann::json*> GetObject(const nlohmann::json& root, std::string_view key, std::string_view where)
{
    const auto it = root.find(key);
    if (it == root.end())
    {
        return nullptr;
    }
    if (!it->is_object())
    {
        return Invalid(std::format("{}: \"{}\" must be an object", where, key));
    }
    return &*it;
}

/// The project layer being loaded, applied only once everything parsed.
struct Staged
{
    std::vector<std::pair<std::string, Value>> values;
    std::vector<std::tuple<std::string, std::string, Value>> characters;
    std::vector<std::tuple<std::string, std::string, DataRow>> rows;
    UnresolvedProjectData unresolved;
    std::map<std::string, std::uint32_t, std::less<>> versions;
};

} // namespace

template <typename Archive>
void Serialize(Archive& archive, CookedValue& value)
{
    archive(value.id, value.value);
}

template <typename Archive>
void Serialize(Archive& archive, CookedAttribute& attribute)
{
    archive(attribute.id, attribute.type, attribute.defaultValue);
}

template <typename Archive>
void Serialize(Archive& archive, CookedArchetype& archetype)
{
    archive(archetype.id, archetype.layoutVersion, archetype.attributes);
}

template <typename Archive>
void Serialize(Archive& archive, CookedColumn& column)
{
    archive(column.name, column.type);
}

template <typename Archive>
void Serialize(Archive& archive, CookedRow& row)
{
    archive(row.id, row.cells);
}

template <typename Archive>
void Serialize(Archive& archive, CookedTable& table)
{
    archive(table.id, table.columns, table.rows);
}

template <typename Archive>
void Serialize(Archive& archive, CookedData& data)
{
    archive(data.values, data.archetypes, data.tables);
}

Result<std::vector<std::string>> LoadProjectData(std::string_view json, DataRegistry& data)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("project data is not a JSON object");
    }
    if (const auto format = root.find("format"); format != root.end())
    {
        if (!format->is_number_unsigned())
        {
            return Invalid("project data: \"format\" must be a non-negative integer");
        }
        if (format->get<std::uint64_t>() > kProjectDataFormat)
        {
            return MakeError(ErrorCode::VersionMismatch,
                             std::format("project data format {} is newer than this engine's ({})",
                                         format->get<std::uint64_t>(), kProjectDataFormat));
        }
    }

    Staged staged;
    std::vector<std::string> warnings;
    INTERI_TRY_ASSIGN(schemas, GetObject(root, "schemas", "project data"));
    for (const std::string_view section : kSections)
    {
        INTERI_TRY_ASSIGN(versions, schemas == nullptr ? Result<const nlohmann::json*>(nullptr)
                                                       : GetObject(*schemas, section, "project data \"schemas\""));
        if (versions == nullptr)
        {
            continue;
        }
        for (const auto& [id, version] : versions->items())
        {
            if (!version.is_number_unsigned() || version.get<std::uint64_t>() > 0xFFFFFFFFu)
            {
                return Invalid(
                    std::format("project data: schema version of {} '{}' must be a non-negative integer", section, id));
            }
            staged.versions.emplace(SchemaKey(section, id), static_cast<std::uint32_t>(version.get<std::uint64_t>()));
        }
    }
    const auto sameVersion = [&](std::string_view section, std::string_view id, std::uint32_t current)
    {
        const auto it = staged.versions.find(SchemaKey(section, id));
        if (it == staged.versions.end() || it->second == current)
        {
            return it != staged.versions.end();
        }
        if (it->second > current)
        {
            warnings.push_back(std::format("'{}' was written against schema version {}, newer than the module's {}", id,
                                           it->second, current));
        }
        return false;
    };

    INTERI_TRY_ASSIGN(values, GetObject(root, "values", "project data"));
    if (values != nullptr)
    {
        for (const auto& [id, value] : values->items())
        {
            const ValueDefinition* definition = data.Values().Definition(id);
            if (definition == nullptr)
            {
                staged.unresolved.values.emplace(id, value.dump());
                warnings.push_back(std::format("value '{}': no loaded module defines it; kept as it is", id));
                continue;
            }
            INTERI_TRY_ASSIGN(read,
                              ReadStored(value, definition->schema, sameVersion("values", id, definition->version),
                                         std::format("value '{}'", id), warnings));
            staged.values.emplace_back(id, std::move(read));
        }
    }

    INTERI_TRY_ASSIGN(characters, GetObject(root, "characters", "project data"));
    if (characters != nullptr)
    {
        for (const auto& [archetype, defaults] : characters->items())
        {
            const ArchetypeDefinition* definition = data.Characters().Archetype(archetype);
            Result<AttributeLayout> layout = data.Characters().Layout(archetype);
            if (definition == nullptr || !layout)
            {
                staged.unresolved.characters.emplace(archetype, defaults.dump());
                warnings.push_back(std::format("archetype '{}': {}; kept as it is", archetype,
                                               definition == nullptr ? std::string("no loaded module defines it")
                                                                     : layout.error().message));
                continue;
            }
            if (!defaults.is_object())
            {
                return Invalid(std::format("archetype '{}' must be an object of attribute id -> value", archetype));
            }
            const bool same = sameVersion("characters", archetype, definition->version);
            for (const auto& [attribute, value] : defaults.items())
            {
                const std::string where = std::format("archetype '{}' attribute '{}'", archetype, attribute);
                const std::size_t slot = layout->Find(attribute);
                if (slot == AttributeLayout::npos)
                {
                    if (same)
                    {
                        return Invalid(std::format("{}: the archetype has no such attribute", where));
                    }
                    warnings.push_back(std::format("{}: the archetype has no such attribute any more; dropped", where));
                    continue;
                }
                INTERI_TRY_ASSIGN(read, ReadStored(value, layout->slots[slot].schema, same, where, warnings));
                staged.characters.emplace_back(archetype, attribute, std::move(read));
            }
        }
    }

    INTERI_TRY_ASSIGN(tables, GetObject(root, "tables", "project data"));
    if (tables != nullptr)
    {
        for (const auto& [table, rows] : tables->items())
        {
            const DataTableDefinition* definition = data.Tables().Table(table);
            if (definition == nullptr)
            {
                staged.unresolved.tables.emplace(table, rows.dump());
                warnings.push_back(std::format("table '{}': no loaded module declares it; kept as it is", table));
                continue;
            }
            if (!rows.is_object())
            {
                return Invalid(std::format("table '{}' must be an object of row id -> cells", table));
            }
            const bool same = sameVersion("tables", table, definition->version);
            for (const auto& [row, cells] : rows.items())
            {
                if (!sdk::IsValidItemName(row) || row.find(':') != std::string::npos)
                {
                    return Invalid(std::format("table '{}': '{}' is not a row id", table, row));
                }
                if (!cells.is_object())
                {
                    return Invalid(
                        std::format("table '{}', row '{}' must be an object of column -> value", table, row));
                }
                DataRow stored;
                for (const auto& [name, value] : cells.items())
                {
                    const ColumnDefinition* column = definition->Find(name);
                    if (column == nullptr)
                    {
                        for (const ColumnDefinition& candidate : definition->columns)
                        {
                            if (std::ranges::find(candidate.renamedFrom, name) != candidate.renamedFrom.end())
                            {
                                column = &candidate;
                            }
                        }
                        if (column != nullptr && cells.contains(column->name))
                        {
                            continue; // the cell under the new name wins
                        }
                        if (column != nullptr)
                        {
                            warnings.push_back(std::format("table '{}', row '{}': column '{}' is now '{}'", table, row,
                                                           name, column->name));
                        }
                    }
                    const std::string where = std::format("table '{}', row '{}', column '{}'", table, row, name);
                    if (column == nullptr)
                    {
                        if (same)
                        {
                            return Invalid(std::format("{}: no such column", where));
                        }
                        warnings.push_back(std::format("{}: the column was removed; dropped", where));
                        continue;
                    }
                    INTERI_TRY_ASSIGN(read, ReadStored(value, column->schema, same, where, warnings));
                    stored.insert_or_assign(column->name, std::move(read));
                }
                staged.rows.emplace_back(table, row, std::move(stored));
            }
        }
    }

    // Everything parsed: replace the project layer.
    data.ClearProject();
    for (auto& [id, value] : staged.values)
    {
        data.Values().LoadProjectValue(id, std::move(value));
    }
    for (auto& [archetype, attribute, value] : staged.characters)
    {
        data.Characters().LoadProjectDefault(archetype, attribute, std::move(value));
    }
    for (auto& [table, row, cells] : staged.rows)
    {
        data.Tables().LoadRow(table, row, std::move(cells));
    }
    data.Unresolved() = std::move(staged.unresolved);
    data.LoadedSchemaVersions() = std::move(staged.versions);
    return warnings;
}

std::string WriteProjectData(const DataRegistry& data)
{
    nlohmann::json root = nlohmann::json::object();
    root["format"] = kProjectDataFormat;
    nlohmann::json values = nlohmann::json::object();
    nlohmann::json characters = nlohmann::json::object();
    nlohmann::json tables = nlohmann::json::object();
    nlohmann::json schemas = nlohmann::json::object();

    // An item is written against its current schema version only if it fits it; otherwise it keeps the version it
    // was loaded with (or 0), so the next load migrates it instead of failing.
    const auto version =
        [&](std::string_view section, const std::string& id, std::optional<std::uint32_t> current, bool fits)
    {
        nlohmann::json& versions = schemas[std::string(section)];
        if (current && fits)
        {
            versions[id] = *current;
            return;
        }
        const auto loaded = data.LoadedSchemaVersions().find(SchemaKey(section, id));
        versions[id] = loaded != data.LoadedSchemaVersions().end() ? loaded->second : 0u;
    };

    for (const auto& [id, project] : data.Values().ProjectValues())
    {
        values[id] = detail::ValueToJson(project.value);
        const ValueDefinition* definition = data.Values().Definition(id);
        version("values", id, definition != nullptr ? std::optional(definition->version) : std::nullopt,
                project.active);
    }
    for (const auto& [archetype, defaults] : data.Characters().ProjectDefaults())
    {
        nlohmann::json object = nlohmann::json::object();
        for (const auto& [attribute, value] : defaults)
        {
            object[attribute] = detail::ValueToJson(value);
        }
        characters[archetype] = std::move(object);
        const ArchetypeDefinition* definition = data.Characters().Archetype(archetype);
        bool fits = true;
        for (const auto& [attribute, value] : defaults)
        {
            fits = fits && data.Characters().CheckProjectDefault(archetype, attribute, value).has_value();
        }
        version("characters", archetype, definition != nullptr ? std::optional(definition->version) : std::nullopt,
                fits);
    }
    for (const auto& [table, rows] : data.Tables().ProjectRows())
    {
        nlohmann::json object = nlohmann::json::object();
        bool fits = true;
        for (const auto& [row, cells] : rows)
        {
            nlohmann::json cellObject = nlohmann::json::object();
            for (const auto& [column, value] : cells)
            {
                cellObject[column] = detail::ValueToJson(value);
            }
            object[row] = std::move(cellObject);
            fits = fits && data.Tables().ValidateRow(table, row).empty();
        }
        tables[table] = std::move(object);
        const DataTableDefinition* definition = data.Tables().Table(table);
        version("tables", table, definition != nullptr ? std::optional(definition->version) : std::nullopt, fits);
    }
    const UnresolvedProjectData& unresolved = data.Unresolved();
    for (const auto& [name, section, entries] : {std::tuple{kSections[0], &values, &unresolved.values},
                                                 std::tuple{kSections[1], &characters, &unresolved.characters},
                                                 std::tuple{kSections[2], &tables, &unresolved.tables}})
    {
        for (const auto& [id, text] : *entries)
        {
            if (!section->contains(id))
            {
                (*section)[id] = nlohmann::json::parse(text, nullptr, false);
                version(name, id, std::nullopt, false);
            }
        }
    }

    root["values"] = std::move(values);
    root["characters"] = std::move(characters);
    root["tables"] = std::move(tables);
    root["schemas"] = std::move(schemas);
    return root.dump(2) + "\n";
}

const Value* CookedData::FindValue(std::string_view id) const noexcept
{
    const auto found = std::ranges::find(values, id, &CookedValue::id);
    return found == values.end() ? nullptr : &found->value;
}

const CookedTable* CookedData::FindTable(std::string_view id) const noexcept
{
    const auto found = std::ranges::find(tables, id, &CookedTable::id);
    return found == tables.end() ? nullptr : &*found;
}

CookedData CookData(const DataRegistry& data, std::vector<std::string>* warnings)
{
    CookedData cooked;
    for (const std::string& id : data.Values().Ids())
    {
        cooked.values.push_back(CookedValue{.id = id, .value = *data.Values().Get(id)});
    }
    for (const std::string& id : data.Characters().ArchetypeIds())
    {
        Result<AttributeLayout> layout = data.Characters().Layout(id);
        if (!layout)
        {
            if (warnings != nullptr)
            {
                warnings->push_back(std::format("archetype '{}' not cooked: {}", id, layout.error().message));
            }
            continue;
        }
        CookedArchetype archetype{.id = id, .layoutVersion = layout->version, .attributes = {}};
        for (const AttributeSlotInfo& slot : layout->slots)
        {
            archetype.attributes.push_back(
                CookedAttribute{.id = slot.id, .type = slot.schema.type, .defaultValue = slot.schema.defaultValue});
        }
        cooked.archetypes.push_back(std::move(archetype));
    }
    for (const std::string& id : data.Tables().TableIds())
    {
        const DataTableDefinition& definition = *data.Tables().Table(id);
        CookedTable table{.id = id, .columns = {}, .rows = {}};
        for (const ColumnDefinition& column : definition.columns)
        {
            table.columns.push_back(CookedColumn{.name = column.name, .type = column.schema.type});
        }
        for (const std::string& row : data.Tables().RowIds(id))
        {
            CookedRow cookedRow{.id = row, .cells = {}};
            for (const ColumnDefinition& column : definition.columns)
            {
                // Every row of a declared table has every column (missing cells read as the default).
                cookedRow.cells.push_back(
                    data.Tables().Cell(id, row, column.name).value_or(column.schema.defaultValue));
            }
            table.rows.push_back(std::move(cookedRow));
        }
        cooked.tables.push_back(std::move(table));
    }
    return cooked;
}

std::vector<std::byte> WriteCookedData(const CookedData& cooked)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kCookedPayloadMagic, kCookedDataVersion);
        CookedData copy = cooked; // Serialize takes non-const references (one function for both directions)
        writer(copy);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kCookedDataMagic, kCookedDataVersion);
    writer(payloadHash, payload);
    return file;
}

Result<CookedData> ReadCookedData(std::span<const std::byte> bytes)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(bytes, kCookedDataMagic, kCookedDataVersion));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "cooked data payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kCookedPayloadMagic, kCookedDataVersion));
    CookedData cooked;
    reader(cooked);
    INTERI_TRY(reader.Finish());
    return cooked;
}

} // namespace interi::values
