// SPDX-License-Identifier: MIT
#include <interi/destruction_cook/CookConfig.h>

#include <nlohmann/json.hpp>

#include <format>
#include <initializer_list>
#include <limits>

namespace interi::destruction
{
namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

void WarnUnknownKeys(const Json& object, std::initializer_list<std::string_view> known, std::string_view context,
                     std::vector<std::string>& warnings)
{
    for (const auto& [key, value] : object.items())
    {
        bool isKnown = false;
        for (const std::string_view k : known)
        {
            isKnown = isKnown || k == key;
        }
        if (!isKnown)
        {
            warnings.push_back(std::format("{}: unknown field \"{}\" ignored", context, key));
        }
    }
}

Result<double> Number(const Json& value, std::string_view what, double min, double max)
{
    if (!value.is_number())
    {
        return Invalid(std::format("{} is not a number", what));
    }
    const double number = value.get<double>();
    if (number < min || number > max) // JSON numbers are never NaN
    {
        return Invalid(std::format("{} = {} is outside [{}, {}]", what, number, min, max));
    }
    return number;
}

Result<std::uint64_t> Unsigned(const Json& value, std::string_view what, std::uint64_t max)
{
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > max)
    {
        return Invalid(std::format("{} is not an unsigned integer <= {}", what, max));
    }
    return value.get<std::uint64_t>();
}

Result<Vec3> Vector(const Json& value, std::string_view what, double min)
{
    if (!value.is_array() || value.size() != 3)
    {
        return Invalid(std::format("{} is not an array of 3 numbers", what));
    }
    Vec3 out;
    for (std::size_t i = 0; i < 3; ++i)
    {
        INTERI_TRY_ASSIGN(component, Number(value[i], what, min, 1e9));
        (i == 0 ? out.x : (i == 1 ? out.y : out.z)) = static_cast<float>(component);
    }
    return out;
}

Result<SeedSettings> ParseLevel(const Json& level, std::string_view context, std::vector<std::string>& warnings)
{
    if (!level.is_object())
    {
        return Invalid(std::format("{} is not an object", context));
    }
    WarnUnknownKeys(level, {"pattern", "count", "seed", "impactPoint", "clusterRadius", "gridCellSize", "jitter"},
                    context, warnings);
    SeedSettings settings;
    if (const auto it = level.find("pattern"); it != level.end())
    {
        const std::string pattern = it->is_string() ? it->get<std::string>() : "";
        if (pattern == "uniform")
        {
            settings.pattern = SeedPattern::Uniform;
        }
        else if (pattern == "clustered")
        {
            settings.pattern = SeedPattern::Clustered;
        }
        else if (pattern == "grid")
        {
            settings.pattern = SeedPattern::Grid;
        }
        else
        {
            return Invalid(std::format(R"({}.pattern must be "uniform", "clustered" or "grid")", context));
        }
    }
    if (const auto it = level.find("count"); it != level.end())
    {
        INTERI_TRY_ASSIGN(count, Unsigned(*it, std::format("{}.count", context), 1u << 16));
        settings.count = static_cast<std::uint32_t>(count);
    }
    if (const auto it = level.find("seed"); it != level.end())
    {
        INTERI_TRY_ASSIGN(seed,
                          Unsigned(*it, std::format("{}.seed", context), std::numeric_limits<std::uint64_t>::max()));
        settings.randomSeed = seed;
    }
    if (const auto it = level.find("impactPoint"); it != level.end())
    {
        INTERI_TRY_ASSIGN(point, Vector(*it, std::format("{}.impactPoint", context), -1e9));
        settings.impactPoint = point;
    }
    if (const auto it = level.find("clusterRadius"); it != level.end())
    {
        INTERI_TRY_ASSIGN(radius, Number(*it, std::format("{}.clusterRadius", context), 1e-6, 1e9));
        settings.clusterRadius = static_cast<float>(radius);
    }
    if (const auto it = level.find("gridCellSize"); it != level.end())
    {
        INTERI_TRY_ASSIGN(size, Vector(*it, std::format("{}.gridCellSize", context), 1e-6));
        settings.gridCellSize = size;
    }
    if (const auto it = level.find("jitter"); it != level.end())
    {
        INTERI_TRY_ASSIGN(jitter, Number(*it, std::format("{}.jitter", context), 0.0, 1.0));
        settings.jitter = static_cast<float>(jitter);
    }
    return settings;
}

/// Applies the fields present in `entry` on top of `settings`.
Result<> ApplyObject(const Json& entry, ObjectCookSettings& settings, std::string_view context,
                     std::vector<std::string>& warnings)
{
    if (!entry.is_object())
    {
        return Invalid(std::format("{} is not an object", context));
    }
    WarnUnknownKeys(entry, {"material", "levels", "minPieceVolume", "anchor", "skip"}, context, warnings);
    if (const auto it = entry.find("material"); it != entry.end())
    {
        if (!it->is_string())
        {
            return Invalid(std::format("{}.material is not a string", context));
        }
        settings.material = it->get<std::string>();
    }
    if (const auto it = entry.find("levels"); it != entry.end())
    {
        if (!it->is_array())
        {
            return Invalid(std::format("{}.levels is not an array", context));
        }
        settings.levels.clear();
        for (std::size_t i = 0; i < it->size(); ++i)
        {
            INTERI_TRY_ASSIGN(level, ParseLevel((*it)[i], std::format("{}.levels[{}]", context, i), warnings));
            settings.levels.push_back(level);
        }
    }
    if (const auto it = entry.find("minPieceVolume"); it != entry.end())
    {
        INTERI_TRY_ASSIGN(volume, Number(*it, std::format("{}.minPieceVolume", context), 0.0, 1e9));
        settings.minPieceVolume = volume;
    }
    for (const auto& [key, member] :
         {std::pair{"anchor", &ObjectCookSettings::anchor}, std::pair{"skip", &ObjectCookSettings::skip}})
    {
        if (const auto it = entry.find(key); it != entry.end())
        {
            if (!it->is_boolean())
            {
                return Invalid(std::format("{}.{} is not a boolean", context, key));
            }
            settings.*member = it->get<bool>();
        }
    }
    return {};
}

Result<> ParseOverrides(const Json& root, const char* key, std::map<std::string, std::string, std::less<>>& out,
                        std::vector<std::string>& warnings)
{
    const auto it = root.find(key);
    if (it == root.end())
    {
        return {};
    }
    if (!it->is_object())
    {
        return Invalid(std::format("\"{}\" is not an object", key));
    }
    for (const auto& [name, entry] : it->items())
    {
        ObjectCookSettings scratch;
        INTERI_TRY(ApplyObject(entry, scratch, std::format("{}.{}", key, name), warnings)); // validate now
        out.emplace(name, entry.dump());
    }
    return {};
}

} // namespace

Result<ObjectCookSettings> CookConfig::SettingsFor(std::string_view nodeName, std::string_view materialName) const
{
    ObjectCookSettings settings = defaults;
    std::vector<std::string> ignored; // reported when the config was parsed
    if (const auto it = surfaceOverrides.find(materialName); it != surfaceOverrides.end())
    {
        INTERI_TRY(ApplyObject(Json::parse(it->second, nullptr, false), settings, it->first, ignored));
    }
    if (const auto it = objectOverrides.find(nodeName); it != objectOverrides.end())
    {
        INTERI_TRY(ApplyObject(Json::parse(it->second, nullptr, false), settings, it->first, ignored));
    }
    return settings;
}

CookConfig DefaultCookConfig()
{
    CookConfig config;
    SeedSettings level;
    level.count = 8;
    config.defaults.levels.push_back(level);
    return config;
}

Result<CookConfig> ParseCookConfig(std::string_view json)
{
    const Json root = Json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("fracture config is not a valid JSON object");
    }
    CookConfig config = DefaultCookConfig();
    WarnUnknownKeys(root,
                    {"materialsFile", "groundHeight", "defaults", "surfaceMaterials", "objects", "hull", "contact"},
                    "config", config.warnings);

    if (const auto it = root.find("materialsFile"); it != root.end())
    {
        if (!it->is_string())
        {
            return Invalid("materialsFile is not a string");
        }
        config.materialsFile = it->get<std::string>();
    }
    if (const auto it = root.find("groundHeight"); it != root.end() && !it->is_null())
    {
        INTERI_TRY_ASSIGN(height, Number(*it, "groundHeight", -1e9, 1e9));
        config.groundHeight = static_cast<float>(height);
    }
    if (const auto it = root.find("defaults"); it != root.end())
    {
        INTERI_TRY(ApplyObject(*it, config.defaults, "defaults", config.warnings));
    }
    INTERI_TRY(ParseOverrides(root, "surfaceMaterials", config.surfaceOverrides, config.warnings));
    INTERI_TRY(ParseOverrides(root, "objects", config.objectOverrides, config.warnings));

    if (const auto it = root.find("hull"); it != root.end())
    {
        if (!it->is_object())
        {
            return Invalid("hull is not an object");
        }
        WarnUnknownKeys(*it, {"maxConcavity", "maxDepth"}, "hull", config.warnings);
        if (const auto c = it->find("maxConcavity"); c != it->end())
        {
            INTERI_TRY_ASSIGN(concavity, Number(*c, "hull.maxConcavity", 0.0, 1e9));
            config.hull.maxConcavity = concavity;
        }
        if (const auto d = it->find("maxDepth"); d != it->end())
        {
            INTERI_TRY_ASSIGN(depth, Unsigned(*d, "hull.maxDepth", 16));
            config.hull.maxDepth = static_cast<std::uint32_t>(depth);
        }
    }
    if (const auto it = root.find("contact"); it != root.end())
    {
        if (!it->is_object())
        {
            return Invalid("contact is not an object");
        }
        WarnUnknownKeys(*it, {"distanceTolerance", "normalTolerance", "minArea"}, "contact", config.warnings);
        if (const auto c = it->find("distanceTolerance"); c != it->end())
        {
            INTERI_TRY_ASSIGN(value, Number(*c, "contact.distanceTolerance", 0.0, 1.0));
            config.contact.distanceTolerance = value;
        }
        if (const auto c = it->find("normalTolerance"); c != it->end())
        {
            INTERI_TRY_ASSIGN(value, Number(*c, "contact.normalTolerance", 0.0, 1.0));
            config.contact.normalTolerance = value;
        }
        if (const auto c = it->find("minArea"); c != it->end())
        {
            INTERI_TRY_ASSIGN(value, Number(*c, "contact.minArea", 0.0, 1e9));
            config.contact.minArea = value;
        }
    }
    return config;
}

} // namespace interi::destruction
