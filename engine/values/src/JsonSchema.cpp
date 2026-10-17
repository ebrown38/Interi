// SPDX-License-Identifier: MIT
#include "JsonSchema.h"

#include <interi/sdk/Manifest.h>

#include "ValueJson.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace interi::values::detail
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

namespace
{

Result<std::optional<double>> GetNumber(const nlohmann::json& object, std::string_view key, std::string_view where)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return std::optional<double>();
    }
    if (!it->is_number())
    {
        return Invalid(std::format("{}: \"{}\" must be a number", where, key));
    }
    return std::optional<double>(it->get<double>());
}

Result<std::string> GetOptionalString(const nlohmann::json& object, std::string_view key, std::string_view where)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return std::string();
    }
    if (!it->is_string())
    {
        return Invalid(std::format("{}: \"{}\" must be a string", where, key));
    }
    return it->get<std::string>();
}

Result<ValueType> GetType(const nlohmann::json& object, std::string_view key, std::string_view where)
{
    INTERI_TRY_ASSIGN(name, GetString(object, key, where));
    const std::optional<ValueType> type = ParseValueType(name);
    if (!type)
    {
        return Invalid(std::format("{}: unknown type '{}'", where, name));
    }
    return *type;
}

} // namespace

Result<std::string> GetString(const nlohmann::json& object, std::string_view key, std::string_view where)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return Invalid(std::format("{}: missing \"{}\"", where, key));
    }
    if (!it->is_string())
    {
        return Invalid(std::format("{}: \"{}\" must be a string", where, key));
    }
    return it->get<std::string>();
}

Result<std::uint32_t> GetU32(const nlohmann::json& object, std::string_view key, std::uint32_t fallback,
                             std::string_view where)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return fallback;
    }
    if (!it->is_number_unsigned() || it->get<std::uint64_t>() > 0xFFFFFFFFu)
    {
        return Invalid(std::format("{}: \"{}\" must be a non-negative integer", where, key));
    }
    return static_cast<std::uint32_t>(it->get<std::uint64_t>());
}

Result<bool> GetBool(const nlohmann::json& object, std::string_view key, bool fallback, std::string_view where)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return fallback;
    }
    if (!it->is_boolean())
    {
        return Invalid(std::format("{}: \"{}\" must be true or false", where, key));
    }
    return it->get<bool>();
}

Result<ValueSchema> ParseSchema(const nlohmann::json& object, std::string_view where)
{
    ValueSchema schema;
    INTERI_TRY_ASSIGN(type, GetType(object, "type", where));
    schema.type = type;
    if (type == ValueType::List)
    {
        INTERI_TRY_ASSIGN(element, GetType(object, "element", where));
        schema.element = element;
    }
    if (const auto options = object.find("options"); options != object.end())
    {
        if (!options->is_array())
        {
            return Invalid(std::format("{}: \"options\" must be an array of strings", where));
        }
        for (const nlohmann::json& option : *options)
        {
            if (!option.is_string())
            {
                return Invalid(std::format("{}: \"options\" must be an array of strings", where));
            }
            schema.options.push_back(option.get<std::string>());
        }
    }
    INTERI_TRY_ASSIGN(min, GetNumber(object, "min", where));
    INTERI_TRY_ASSIGN(max, GetNumber(object, "max", where));
    INTERI_TRY_ASSIGN(step, GetNumber(object, "step", where));
    schema.meta.min = min;
    schema.meta.max = max;
    schema.meta.step = step.value_or(0.0);
    INTERI_TRY_ASSIGN(unit, GetOptionalString(object, "unit", where));
    INTERI_TRY_ASSIGN(category, GetOptionalString(object, "category", where));
    INTERI_TRY_ASSIGN(tooltip, GetOptionalString(object, "tooltip", where));
    INTERI_TRY_ASSIGN(readOnly, GetBool(object, "readOnly", false, where));
    schema.meta.unit = std::move(unit);
    schema.meta.category = std::move(category);
    schema.meta.tooltip = std::move(tooltip);
    schema.meta.readOnly = readOnly;

    if (const auto value = object.find("default"); value != object.end())
    {
        INTERI_TRY_ASSIGN(parsed, ValueFromJson(*value, schema, std::format("{}: \"default\"", where)));
        schema.defaultValue = std::move(parsed);
    }
    else if (type == ValueType::Enum && !schema.options.empty())
    {
        schema.defaultValue = EnumValue{.name = schema.options.front()};
    }
    else
    {
        schema.defaultValue = ZeroValue(schema.type, schema.element);
    }
    if (const Result<void> valid = ValidateSchema(schema); !valid)
    {
        return Invalid(std::format("{}: {}", where, valid.error().message));
    }
    return schema;
}

void WarnUnknownKeys(const nlohmann::json& object, std::span<const std::string_view> known,
                     std::span<const std::string_view> alsoKnown, std::string_view where,
                     std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : object.items())
    {
        if (std::ranges::find(known, std::string_view(key)) == known.end() &&
            std::ranges::find(alsoKnown, std::string_view(key)) == alsoKnown.end())
        {
            warnings.push_back(std::format("{}: unknown key \"{}\" ignored", where, key));
        }
    }
}

Result<std::string> ParseItemId(const nlohmann::json& object, std::string_view module, std::string_view where,
                                bool& isOverride)
{
    INTERI_TRY_ASSIGN(shared, GetBool(object, "shared", false, where));
    const bool hasId = object.contains("id");
    isOverride = object.contains("overrides");
    if (hasId == isOverride)
    {
        return Invalid(std::format(R"({}: needs exactly one of "id" and "overrides")", where));
    }
    if (isOverride)
    {
        if (shared)
        {
            return Invalid(std::format("{}: an override can't be shared (shared ids need no override)", where));
        }
        INTERI_TRY_ASSIGN(target, GetString(object, "overrides", where));
        if (!sdk::IsLocalId(target))
        {
            return Invalid(std::format("{}: \"overrides\" '{}' is not a local id (<module id>:<name>)", where, target));
        }
        return target;
    }
    INTERI_TRY_ASSIGN(id, GetString(object, "id", where));
    if (!sdk::IsValidItemName(id) || id.find(':') != std::string::npos)
    {
        return Invalid(std::format("{}: '{}' is not an item name (letters, digits and \"._-\")", where, id));
    }
    return shared ? id : std::format("{}:{}", module, id);
}

Result<std::string> ReadTextFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("can't read {}", path));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace interi::values::detail
