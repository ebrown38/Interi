// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/gameplay/WeaponDef.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

namespace interi::gameplay
{

namespace
{

constexpr std::uint8_t kMelee = 1u << static_cast<unsigned>(WeaponKind::Melee);
constexpr std::uint8_t kCharge = 1u << static_cast<unsigned>(WeaponKind::RemoteCharge);
constexpr std::uint8_t kRocket = 1u << static_cast<unsigned>(WeaponKind::Rocket);
constexpr std::uint8_t kAll = kMelee | kCharge | kRocket;

struct FloatField
{
    std::string_view key;
    float WeaponDef::* member;
    std::uint8_t kinds; ///< Kinds that use the field (a missing one warns only for those).
};

constexpr FloatField kFloatFields[] = {
    {"energy", &WeaponDef::energy, kAll},     {"innerRadius", &WeaponDef::innerRadius, kAll},
    {"radius", &WeaponDef::radius, kAll},     {"impulse", &WeaponDef::impulse, kAll},
    {"cooldown", &WeaponDef::cooldown, kAll}, {"range", &WeaponDef::range, kAll},
    {"speed", &WeaponDef::speed, kRocket},    {"hitRadius", &WeaponDef::hitRadius, kMelee | kRocket},
};

constexpr std::string_view kMaxChargesKey = "maxCharges";

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

std::optional<WeaponKind> ParseKind(std::string_view text)
{
    for (const WeaponKind kind : {WeaponKind::Melee, WeaponKind::RemoteCharge, WeaponKind::Rocket})
    {
        if (ToString(kind) == text)
        {
            return kind;
        }
    }
    return std::nullopt;
}

bool IsKnownKey(std::string_view key)
{
    return key == "name" || key == "kind" || key == kMaxChargesKey ||
           std::ranges::any_of(kFloatFields, [key](const FloatField& field) { return field.key == key; });
}

Result<WeaponDef> ParseWeapon(const nlohmann::json& entry, std::size_t index, std::vector<std::string>& warnings)
{
    if (!entry.is_object())
    {
        return Invalid(std::format("weapons[{}] is not an object", index));
    }
    const auto nameIt = entry.find("name");
    if (nameIt == entry.end() || !nameIt->is_string())
    {
        return Invalid(std::format("weapons[{}] has no string \"name\"", index));
    }
    WeaponDef weapon;
    weapon.name = nameIt->get<std::string>();
    const auto kindIt = entry.find("kind");
    if (kindIt == entry.end() || !kindIt->is_string())
    {
        return Invalid(std::format("weapon '{}' has no string \"kind\"", weapon.name));
    }
    const std::optional<WeaponKind> kind = ParseKind(kindIt->get<std::string>());
    if (!kind)
    {
        return Invalid(std::format("weapon '{}': unknown kind \"{}\" (melee, remoteCharge, rocket)", weapon.name,
                                   kindIt->get<std::string>()));
    }
    weapon.kind = *kind;
    if (weapon.kind == WeaponKind::RemoteCharge)
    {
        weapon.maxCharges = 1;
    }
    const auto kindBit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(weapon.kind));

    for (const FloatField& field : kFloatFields)
    {
        const auto it = entry.find(field.key);
        if (it == entry.end())
        {
            if ((field.kinds & kindBit) != 0)
            {
                warnings.push_back(std::format("weapon '{}': missing \"{}\", using default {}", weapon.name, field.key,
                                               weapon.*field.member));
            }
            continue;
        }
        if (!it->is_number())
        {
            return Invalid(std::format("weapon '{}': \"{}\" is not a number", weapon.name, field.key));
        }
        const double value = it->get<double>();
        if (!(std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max())))
        {
            return Invalid(std::format("weapon '{}': \"{}\" is out of float range", weapon.name, field.key));
        }
        weapon.*field.member = static_cast<float>(value);
    }
    if (const auto it = entry.find(kMaxChargesKey); it != entry.end())
    {
        if (!it->is_number_unsigned() || it->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
        {
            return Invalid(
                std::format("weapon '{}': \"{}\" is not a 32-bit unsigned integer", weapon.name, kMaxChargesKey));
        }
        weapon.maxCharges = it->get<std::uint32_t>();
    }
    else if (weapon.kind == WeaponKind::RemoteCharge)
    {
        warnings.push_back(std::format("weapon '{}': missing \"{}\", using default {}", weapon.name, kMaxChargesKey,
                                       weapon.maxCharges));
    }
    for (const auto& [key, value] : entry.items())
    {
        if (!IsKnownKey(key))
        {
            warnings.push_back(std::format("weapon '{}': unknown field \"{}\" ignored", weapon.name, key));
        }
    }
    INTERI_TRY(Validate(weapon));
    return weapon;
}

} // namespace

std::string_view ToString(WeaponKind kind) noexcept
{
    switch (kind)
    {
    case WeaponKind::Melee:
        return "melee";
    case WeaponKind::RemoteCharge:
        return "remoteCharge";
    case WeaponKind::Rocket:
        return "rocket";
    }
    return "unknown";
}

Result<> Validate(const WeaponDef& weapon)
{
    if (weapon.name.empty())
    {
        return Invalid("weapon has an empty name");
    }
    const auto check = [&](std::string_view field, float value, bool ok) -> Result<>
    {
        if (!std::isfinite(value) || !ok)
        {
            return Invalid(std::format("weapon '{}': invalid \"{}\" ({})", weapon.name, field, value));
        }
        return {};
    };
    INTERI_TRY(check("energy", weapon.energy, weapon.energy >= 0.0f));
    INTERI_TRY(check("innerRadius", weapon.innerRadius, weapon.innerRadius > 0.0f));
    INTERI_TRY(check("radius", weapon.radius, weapon.radius > weapon.innerRadius));
    INTERI_TRY(check("impulse", weapon.impulse, weapon.impulse >= 0.0f));
    INTERI_TRY(check("cooldown", weapon.cooldown, weapon.cooldown >= 0.0f));
    INTERI_TRY(check("range", weapon.range, weapon.range > 0.0f));
    INTERI_TRY(check("hitRadius", weapon.hitRadius, weapon.hitRadius >= 0.0f));
    INTERI_TRY(check("speed", weapon.speed, weapon.kind != WeaponKind::Rocket || weapon.speed > 0.0f));
    if (weapon.kind == WeaponKind::RemoteCharge && weapon.maxCharges == 0)
    {
        return Invalid(std::format("weapon '{}': \"maxCharges\" must be >= 1", weapon.name));
    }
    return {};
}

Result<WeaponDefSet> ParseWeaponDefs(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded())
    {
        return Invalid("weapons file is not valid JSON");
    }
    const auto list = root.is_object() ? root.find("weapons") : root.end();
    if (!root.is_object() || list == root.end() || !list->is_array())
    {
        return Invalid("weapons file has no \"weapons\" array");
    }
    WeaponDefSet set;
    for (std::size_t i = 0; i < list->size(); ++i)
    {
        INTERI_TRY_ASSIGN(weapon, ParseWeapon((*list)[i], i, set.warnings));
        if (FindWeapon(set.weapons, weapon.name) != nullptr)
        {
            return Invalid(std::format("duplicate weapon name '{}'", weapon.name));
        }
        set.weapons.push_back(std::move(weapon));
    }
    for (const std::string& warning : set.warnings)
    {
        INTERI_LOG_WARN("Gameplay", "{}", warning);
    }
    return set;
}

Result<WeaponDefSet> LoadWeaponDefs(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot read '{}'", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseWeaponDefs(text.str());
}

const WeaponDef* FindWeapon(std::span<const WeaponDef> weapons, std::string_view name) noexcept
{
    const auto found = std::ranges::find(weapons, name, &WeaponDef::name);
    return found != weapons.end() ? &*found : nullptr;
}

} // namespace interi::gameplay
