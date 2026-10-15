// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/destruction/Material.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

namespace interi::destruction
{
namespace
{

struct FloatField
{
    std::string_view key;
    float DestructionMaterial::* member;
};

constexpr FloatField kFloatFields[] = {
    {"density", &DestructionMaterial::density},
    {"compressiveStrength", &DestructionMaterial::compressiveStrength},
    {"tensileStrength", &DestructionMaterial::tensileStrength},
    {"shearStrength", &DestructionMaterial::shearStrength},
    {"fractureEnergy", &DestructionMaterial::fractureEnergy},
    {"debrisMinSize", &DestructionMaterial::debrisMinSize},
};

constexpr std::string_view kInteriorKey = "interiorMaterialId";

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsKnownKey(std::string_view key)
{
    if (key == "name" || key == kInteriorKey)
    {
        return true;
    }
    for (const FloatField& field : kFloatFields)
    {
        if (field.key == key)
        {
            return true;
        }
    }
    return false;
}

Result<DestructionMaterial> ParseMaterial(const nlohmann::json& entry, std::size_t index,
                                          std::vector<std::string>& warnings)
{
    if (!entry.is_object())
    {
        return Invalid(std::format("materials[{}] is not an object", index));
    }
    const auto nameIt = entry.find("name");
    if (nameIt == entry.end() || !nameIt->is_string())
    {
        return Invalid(std::format("materials[{}] has no string \"name\"", index));
    }

    DestructionMaterial material;
    material.name = nameIt->get<std::string>();
    for (const FloatField& field : kFloatFields)
    {
        const auto it = entry.find(field.key);
        if (it == entry.end())
        {
            warnings.push_back(std::format("material '{}': missing \"{}\", using default {}", material.name, field.key,
                                           material.*field.member));
            continue;
        }
        if (!it->is_number())
        {
            return Invalid(std::format("material '{}': \"{}\" is not a number", material.name, field.key));
        }
        const double value = it->get<double>();
        if (!(std::abs(value) <= static_cast<double>(std::numeric_limits<float>::max())))
        {
            return Invalid(std::format("material '{}': \"{}\" is out of float range", material.name, field.key));
        }
        material.*field.member = static_cast<float>(value);
    }

    const auto interiorIt = entry.find(kInteriorKey);
    if (interiorIt == entry.end())
    {
        warnings.push_back(std::format("material '{}': missing \"{}\", using default {}", material.name, kInteriorKey,
                                       material.interiorMaterialId));
    }
    else if (!interiorIt->is_number_unsigned() ||
             interiorIt->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    {
        return Invalid(
            std::format("material '{}': \"{}\" is not a 32-bit unsigned integer", material.name, kInteriorKey));
    }
    else
    {
        material.interiorMaterialId = interiorIt->get<std::uint32_t>();
    }

    for (const auto& [key, value] : entry.items())
    {
        if (!IsKnownKey(key))
        {
            warnings.push_back(std::format("material '{}': unknown field \"{}\" ignored", material.name, key));
        }
    }

    INTERI_TRY(Validate(material));
    return material;
}

} // namespace

Result<> Validate(const DestructionMaterial& material)
{
    if (material.name.empty())
    {
        return Invalid("material has an empty name");
    }
    const auto check = [&](std::string_view field, float value, bool allowZero) -> Result<>
    {
        const bool ok = std::isfinite(value) && (allowZero ? value >= 0.0f : value > 0.0f);
        if (!ok)
        {
            return Invalid(std::format("material '{}': \"{}\" must be {} (got {})", material.name, field,
                                       allowZero ? ">= 0" : "> 0", value));
        }
        return {};
    };
    INTERI_TRY(check("density", material.density, false));
    INTERI_TRY(check("compressiveStrength", material.compressiveStrength, false));
    INTERI_TRY(check("tensileStrength", material.tensileStrength, false));
    INTERI_TRY(check("shearStrength", material.shearStrength, false));
    INTERI_TRY(check("fractureEnergy", material.fractureEnergy, true));
    INTERI_TRY(check("debrisMinSize", material.debrisMinSize, true));
    return {};
}

Result<MaterialSet> ParseMaterials(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded())
    {
        return Invalid("materials file is not valid JSON");
    }
    const auto listIt = root.is_object() ? root.find("materials") : root.end();
    if (listIt == root.end() || !listIt->is_array())
    {
        return Invalid("materials file has no \"materials\" array");
    }

    MaterialSet set;
    for (std::size_t i = 0; i < listIt->size(); ++i)
    {
        INTERI_TRY_ASSIGN(material, ParseMaterial((*listIt)[i], i, set.warnings));
        if (FindMaterial(set.materials, material.name))
        {
            return Invalid(std::format("duplicate material '{}'", material.name));
        }
        set.materials.push_back(std::move(material));
    }
    for (const std::string& warning : set.warnings)
    {
        INTERI_LOG_WARN("destruction", "{}", warning);
    }
    return set;
}

Result<MaterialSet> LoadMaterials(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot open materials file '{}'", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseMaterials(text.str());
}

std::optional<std::uint32_t> FindMaterial(std::span<const DestructionMaterial> materials,
                                          std::string_view name) noexcept
{
    for (std::size_t i = 0; i < materials.size(); ++i)
    {
        if (materials[i].name == name)
        {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

} // namespace interi::destruction
