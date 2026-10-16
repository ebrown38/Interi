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

namespace interi::gameplay
{

enum class WeaponKind : std::uint8_t
{
    Melee,        ///< Sledgehammer: a short sphere sweep; impulse at the contact and melee damage there.
    RemoteCharge, ///< Placed on a surface (sticks to what it hit), detonated later in a radial blast.
    Rocket,       ///< A projectile flying straight at `speed`; blasts where it hits or at `range`.
};

[[nodiscard]] std::string_view ToString(WeaponKind kind) noexcept;

/// A weapon or tool, loaded from data. Damage uses the destruction model's energy units: `energy` joules are dealt
/// to every bond within `innerRadius`, falling off inverse-square out to `radius` (RadialDamage).
struct WeaponDef
{
    std::string name;
    WeaponKind kind = WeaponKind::Melee;
    float energy = 0.0f;          ///< J per bond at the centre.
    float innerRadius = 0.5f;     ///< m (> 0).
    float radius = 2.0f;          ///< m (> innerRadius).
    float impulse = 0.0f;         ///< N·s: melee at the contact point; blasts at the centre, falling off like damage.
    float cooldown = 0.5f;        ///< s between uses.
    float range = 2.0f;           ///< m: melee reach, charge placement reach, rocket flight distance.
    float speed = 0.0f;           ///< m/s, rockets.
    float hitRadius = 0.0f;       ///< m: melee sweep sphere / rocket body radius (0 = a ray).
    std::uint32_t maxCharges = 0; ///< Remote charges placed at once (placing more removes the oldest).
};

/// Weapons parsed from a data file, in file order, plus non-fatal problems (each also logged).
struct WeaponDefSet
{
    std::vector<WeaponDef> weapons;
    std::vector<std::string> warnings;
};

/// Checks the ranges: energy, impulse, cooldown >= 0; 0 < innerRadius < radius; range > 0; rockets speed > 0;
/// remote charges maxCharges >= 1; hitRadius >= 0. InvalidArgument names the weapon and field.
[[nodiscard]] Result<> Validate(const WeaponDef& weapon);

/// Parses a weapons file:
///
///     { "weapons": [ { "name": "sledgehammer", "kind": "melee", "energy": 4000, "radius": 1.2, ... } ] }
///
/// `name` and `kind` ("melee", "remoteCharge", "rocket") are required. A missing field that the kind uses takes its
/// default and adds a warning, as does an unknown field. Malformed JSON, a wrong field type, a duplicate name or an
/// invalid value (Validate) fails with InvalidArgument.
[[nodiscard]] Result<WeaponDefSet> ParseWeaponDefs(std::string_view json);

/// Reads and parses a weapons file. IoError if it cannot be read.
[[nodiscard]] Result<WeaponDefSet> LoadWeaponDefs(const std::filesystem::path& path);

[[nodiscard]] const WeaponDef* FindWeapon(std::span<const WeaponDef> weapons, std::string_view name) noexcept;

} // namespace interi::gameplay
