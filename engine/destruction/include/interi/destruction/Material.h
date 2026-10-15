// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::destruction
{

/// Physical properties of a destructible material. The defaults describe generic concrete. Strengths are stresses
/// (Pa): a bond's force limit is its contact area times the strength.
struct DestructionMaterial
{
    std::string name;
    float density = 2400.0f;              ///< kg/m^3.
    float compressiveStrength = 30.0e6f;  ///< Pa.
    float tensileStrength = 3.0e6f;       ///< Pa.
    float shearStrength = 4.0e6f;         ///< Pa.
    float fractureEnergy = 100.0f;        ///< J/m^2 of new crack surface.
    float debrisMinSize = 0.1f;           ///< m; fracture pieces smaller than this are merged away.
    std::uint32_t interiorMaterialId = 0; ///< Render material of faces exposed by fracture.

    friend bool operator==(const DestructionMaterial&, const DestructionMaterial&) = default;
};

template <typename Archive>
void Serialize(Archive& archive, DestructionMaterial& material)
{
    archive(material.name, material.density, material.compressiveStrength, material.tensileStrength,
            material.shearStrength, material.fractureEnergy, material.debrisMinSize, material.interiorMaterialId);
}

/// Checks that every physical value is finite and in range: density and strengths > 0, fractureEnergy and
/// debrisMinSize >= 0, and a non-empty name. Fails with InvalidArgument naming the field.
[[nodiscard]] Result<> Validate(const DestructionMaterial& material);

/// Materials parsed from a data file, in file order, plus non-fatal problems (missing fields that took their
/// default, unknown fields). Each warning is also logged.
struct MaterialSet
{
    std::vector<DestructionMaterial> materials;
    std::vector<std::string> warnings;
};

/// Parses a materials file:
///
///     { "materials": [ { "name": "concrete", "density": 2400, "compressiveStrength": 3.0e7, ... } ] }
///
/// A missing field takes its default value and adds a warning. Malformed JSON, a wrong field type, a duplicate or
/// missing name, or an out-of-range value (see Validate) fails with InvalidArgument.
[[nodiscard]] Result<MaterialSet> ParseMaterials(std::string_view json);

/// Reads and parses a materials file. Fails with IoError if it cannot be read.
[[nodiscard]] Result<MaterialSet> LoadMaterials(const std::filesystem::path& path);

/// Index of the material with this name.
[[nodiscard]] std::optional<std::uint32_t> FindMaterial(std::span<const DestructionMaterial> materials,
                                                        std::string_view name) noexcept;

} // namespace interi::destruction
