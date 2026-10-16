// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/gameplay/VehicleDef.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

namespace interi::gameplay
{

namespace
{

struct FloatField
{
    std::string_view key;
    float VehicleDef::* member;
};

constexpr FloatField kFloatFields[] = {
    {"mass", &VehicleDef::mass},
    {"wheelRadius", &VehicleDef::wheelRadius},
    {"wheelWidth", &VehicleDef::wheelWidth},
    {"wheelMass", &VehicleDef::wheelMass},
    {"suspensionMinLength", &VehicleDef::suspensionMinLength},
    {"suspensionMaxLength", &VehicleDef::suspensionMaxLength},
    {"suspensionFrequency", &VehicleDef::suspensionFrequency},
    {"suspensionDamping", &VehicleDef::suspensionDamping},
    {"maxSteerDegrees", &VehicleDef::maxSteerDegrees},
    {"engineTorque", &VehicleDef::engineTorque},
    {"engineMinRpm", &VehicleDef::engineMinRpm},
    {"engineMaxRpm", &VehicleDef::engineMaxRpm},
    {"reverseGearRatio", &VehicleDef::reverseGearRatio},
    {"differentialRatio", &VehicleDef::differentialRatio},
    {"brakeTorque", &VehicleDef::brakeTorque},
    {"handBrakeTorque", &VehicleDef::handBrakeTorque},
    {"health", &VehicleDef::health},
    {"impactThreshold", &VehicleDef::impactThreshold},
    {"impactDamagePerImpulse", &VehicleDef::impactDamagePerImpulse},
    {"explosionDamagePerJoule", &VehicleDef::explosionDamagePerJoule},
};

struct VecField
{
    std::string_view key;
    Vec3 VehicleDef::* member;
};

constexpr VecField kVecFields[] = {
    {"halfExtents", &VehicleDef::halfExtents},
    {"centerOfMassOffset", &VehicleDef::centerOfMassOffset},
};

constexpr std::string_view kDriveKey = "drive";
constexpr std::string_view kGearsKey = "gearRatios";

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

std::optional<DriveLayout> ParseDrive(std::string_view text)
{
    for (const DriveLayout drive : {DriveLayout::Front, DriveLayout::Rear, DriveLayout::All})
    {
        if (ToString(drive) == text)
        {
            return drive;
        }
    }
    return std::nullopt;
}

bool IsKnownKey(std::string_view key)
{
    return key == "name" || key == kDriveKey || key == kGearsKey ||
           std::ranges::any_of(kFloatFields, [key](const FloatField& field) { return field.key == key; }) ||
           std::ranges::any_of(kVecFields, [key](const VecField& field) { return field.key == key; });
}

std::optional<float> ToFloat(const nlohmann::json& value)
{
    if (!value.is_number())
    {
        return std::nullopt;
    }
    const double number = value.get<double>();
    if (!(std::abs(number) <= static_cast<double>(std::numeric_limits<float>::max())))
    {
        return std::nullopt;
    }
    return static_cast<float>(number);
}

Result<VehicleDef> ParseVehicle(const nlohmann::json& entry, std::size_t index, std::vector<std::string>& warnings)
{
    if (!entry.is_object())
    {
        return Invalid(std::format("vehicles[{}] is not an object", index));
    }
    const auto nameIt = entry.find("name");
    if (nameIt == entry.end() || !nameIt->is_string())
    {
        return Invalid(std::format("vehicles[{}] has no string \"name\"", index));
    }
    VehicleDef vehicle;
    vehicle.name = nameIt->get<std::string>();
    const auto missing = [&](std::string_view key)
    { warnings.push_back(std::format("vehicle '{}': missing \"{}\", using the default", vehicle.name, key)); };

    for (const FloatField& field : kFloatFields)
    {
        const auto it = entry.find(field.key);
        if (it == entry.end())
        {
            missing(field.key);
            continue;
        }
        const std::optional<float> value = ToFloat(*it);
        if (!value)
        {
            return Invalid(std::format("vehicle '{}': \"{}\" is not a float", vehicle.name, field.key));
        }
        vehicle.*field.member = *value;
    }
    for (const VecField& field : kVecFields)
    {
        const auto it = entry.find(field.key);
        if (it == entry.end())
        {
            missing(field.key);
            continue;
        }
        if (!it->is_array() || it->size() != 3)
        {
            return Invalid(std::format("vehicle '{}': \"{}\" is not an [x, y, z] array", vehicle.name, field.key));
        }
        float xyz[3] = {};
        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::optional<float> value = ToFloat((*it)[i]);
            if (!value)
            {
                return Invalid(std::format("vehicle '{}': \"{}\" is not an [x, y, z] array", vehicle.name, field.key));
            }
            xyz[i] = *value;
        }
        vehicle.*field.member = Vec3(xyz[0], xyz[1], xyz[2]);
    }
    if (const auto it = entry.find(kDriveKey); it != entry.end())
    {
        const std::optional<DriveLayout> drive =
            it->is_string() ? ParseDrive(it->get<std::string>()) : std::optional<DriveLayout>{};
        if (!drive)
        {
            return Invalid(std::format("vehicle '{}': \"drive\" is not one of front, rear, all", vehicle.name));
        }
        vehicle.drive = *drive;
    }
    else
    {
        missing(kDriveKey);
    }
    if (const auto it = entry.find(kGearsKey); it != entry.end())
    {
        if (!it->is_array())
        {
            return Invalid(std::format("vehicle '{}': \"{}\" is not an array", vehicle.name, kGearsKey));
        }
        vehicle.gearRatios.clear();
        for (const nlohmann::json& gear : *it)
        {
            const std::optional<float> value = ToFloat(gear);
            if (!value)
            {
                return Invalid(std::format("vehicle '{}': \"{}\" holds a non-number", vehicle.name, kGearsKey));
            }
            vehicle.gearRatios.push_back(*value);
        }
    }
    else
    {
        missing(kGearsKey);
    }
    for (const auto& [key, value] : entry.items())
    {
        if (!IsKnownKey(key))
        {
            warnings.push_back(std::format("vehicle '{}': unknown field \"{}\" ignored", vehicle.name, key));
        }
    }
    INTERI_TRY(Validate(vehicle));
    return vehicle;
}

} // namespace

std::string_view ToString(DriveLayout drive) noexcept
{
    switch (drive)
    {
    case DriveLayout::Front:
        return "front";
    case DriveLayout::Rear:
        return "rear";
    case DriveLayout::All:
        return "all";
    }
    return "unknown";
}

Result<> Validate(const VehicleDef& vehicle)
{
    if (vehicle.name.empty())
    {
        return Invalid("vehicle has an empty name");
    }
    const auto check = [&](std::string_view field, float value, bool ok) -> Result<>
    {
        if (!std::isfinite(value) || !ok)
        {
            return Invalid(std::format("vehicle '{}': invalid \"{}\" ({})", vehicle.name, field, value));
        }
        return {};
    };
    const Vec3& e = vehicle.halfExtents;
    INTERI_TRY(check("mass", vehicle.mass, vehicle.mass > 0.0f));
    INTERI_TRY(check("halfExtents", e.x + e.y + e.z, e.x > 0.0f && e.y > 0.0f && e.z > 0.0f));
    const Vec3& com = vehicle.centerOfMassOffset;
    INTERI_TRY(check("centerOfMassOffset", com.x + com.y + com.z, true));
    INTERI_TRY(check("wheelRadius", vehicle.wheelRadius, vehicle.wheelRadius > 0.0f));
    INTERI_TRY(check("wheelWidth", vehicle.wheelWidth, vehicle.wheelWidth > 0.0f));
    INTERI_TRY(
        check("wheelMass", vehicle.wheelMass, vehicle.wheelMass > 0.0f && 4.0f * vehicle.wheelMass < vehicle.mass));
    INTERI_TRY(check("suspensionMinLength", vehicle.suspensionMinLength, vehicle.suspensionMinLength >= 0.0f));
    INTERI_TRY(check("suspensionMaxLength", vehicle.suspensionMaxLength,
                     vehicle.suspensionMaxLength > vehicle.suspensionMinLength));
    INTERI_TRY(check("suspensionFrequency", vehicle.suspensionFrequency, vehicle.suspensionFrequency > 0.0f));
    INTERI_TRY(check("suspensionDamping", vehicle.suspensionDamping, vehicle.suspensionDamping >= 0.0f));
    INTERI_TRY(check("maxSteerDegrees", vehicle.maxSteerDegrees,
                     vehicle.maxSteerDegrees >= 0.0f && vehicle.maxSteerDegrees < 90.0f));
    INTERI_TRY(check("engineTorque", vehicle.engineTorque, vehicle.engineTorque > 0.0f));
    INTERI_TRY(check("engineMinRpm", vehicle.engineMinRpm, vehicle.engineMinRpm > 0.0f));
    INTERI_TRY(check("engineMaxRpm", vehicle.engineMaxRpm, vehicle.engineMaxRpm > vehicle.engineMinRpm));
    if (vehicle.gearRatios.empty())
    {
        return Invalid(std::format("vehicle '{}': \"gearRatios\" is empty", vehicle.name));
    }
    for (const float gear : vehicle.gearRatios)
    {
        INTERI_TRY(check("gearRatios", gear, gear > 0.0f));
    }
    INTERI_TRY(check("reverseGearRatio", vehicle.reverseGearRatio, vehicle.reverseGearRatio < 0.0f));
    INTERI_TRY(check("differentialRatio", vehicle.differentialRatio, vehicle.differentialRatio > 0.0f));
    INTERI_TRY(check("brakeTorque", vehicle.brakeTorque, vehicle.brakeTorque >= 0.0f));
    INTERI_TRY(check("handBrakeTorque", vehicle.handBrakeTorque, vehicle.handBrakeTorque >= 0.0f));
    INTERI_TRY(check("health", vehicle.health, vehicle.health > 0.0f));
    INTERI_TRY(check("impactThreshold", vehicle.impactThreshold, vehicle.impactThreshold >= 0.0f));
    INTERI_TRY(check("impactDamagePerImpulse", vehicle.impactDamagePerImpulse, vehicle.impactDamagePerImpulse >= 0.0f));
    INTERI_TRY(
        check("explosionDamagePerJoule", vehicle.explosionDamagePerJoule, vehicle.explosionDamagePerJoule >= 0.0f));
    return {};
}

Result<VehicleDefSet> ParseVehicleDefs(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded())
    {
        return Invalid("vehicles file is not valid JSON");
    }
    const auto list = root.is_object() ? root.find("vehicles") : root.end();
    if (!root.is_object() || list == root.end() || !list->is_array())
    {
        return Invalid("vehicles file has no \"vehicles\" array");
    }
    VehicleDefSet set;
    for (std::size_t i = 0; i < list->size(); ++i)
    {
        INTERI_TRY_ASSIGN(vehicle, ParseVehicle((*list)[i], i, set.warnings));
        if (FindVehicle(set.vehicles, vehicle.name) != nullptr)
        {
            return Invalid(std::format("duplicate vehicle name '{}'", vehicle.name));
        }
        set.vehicles.push_back(std::move(vehicle));
    }
    for (const std::string& warning : set.warnings)
    {
        INTERI_LOG_WARN("Gameplay", "{}", warning);
    }
    return set;
}

Result<VehicleDefSet> LoadVehicleDefs(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot read '{}'", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseVehicleDefs(text.str());
}

const VehicleDef* FindVehicle(std::span<const VehicleDef> vehicles, std::string_view name) noexcept
{
    const auto found = std::ranges::find(vehicles, name, &VehicleDef::name);
    return found != vehicles.end() ? &*found : nullptr;
}

} // namespace interi::gameplay
