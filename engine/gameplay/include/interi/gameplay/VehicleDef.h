// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::gameplay
{

enum class DriveLayout : std::uint8_t
{
    Front, ///< Front (steering) wheels driven.
    Rear,
    All, ///< Engine torque split evenly between the front and rear differentials.
};

[[nodiscard]] std::string_view ToString(DriveLayout drive) noexcept;

/// A four-wheeled vehicle, loaded from data. Vehicle space is Jolt's vehicle convention: +Z forward, +Y up, +X left.
/// The wheels sit at the chassis box's four bottom corners: x = ±halfExtents.x, y = -halfExtents.y,
/// z = ±(halfExtents.z - wheelRadius); the front (+Z) pair steers.
struct VehicleDef
{
    std::string name;
    float mass = 1500.0f;                       ///< kg, whole vehicle.
    Vec3 halfExtents{0.9f, 0.4f, 2.1f};         ///< m, of the chassis box.
    Vec3 centerOfMassOffset{0.0f, -0.4f, 0.0f}; ///< m, from the box centre (low = stable).
    float wheelRadius = 0.4f;                   ///< m.
    float wheelWidth = 0.3f;                    ///< m.
    float wheelMass = 25.0f;                    ///< kg of each wheel when the vehicle turns into wreck debris.
    float suspensionMinLength = 0.2f;           ///< m.
    float suspensionMaxLength = 0.5f;           ///< m.
    float suspensionFrequency = 1.5f;           ///< Hz.
    float suspensionDamping = 0.5f;             ///< Ratio (1 = critical).
    float maxSteerDegrees = 30.0f;
    DriveLayout drive = DriveLayout::Rear;
    float engineTorque = 500.0f; ///< N·m, maximum.
    float engineMinRpm = 1000.0f;
    float engineMaxRpm = 6000.0f;
    std::vector<float> gearRatios{2.66f, 1.78f, 1.3f, 1.0f, 0.74f}; ///< Forward gears, first to last (> 0).
    float reverseGearRatio = -2.9f;                                 ///< (< 0).
    float differentialRatio = 3.42f;
    float brakeTorque = 1500.0f;     ///< N·m per wheel at full brake.
    float handBrakeTorque = 4000.0f; ///< N·m per rear wheel.
    float health = 1000.0f;
    /// Contacts on the vehicle with a larger estimated impulse (N·s) damage it by (impulse - threshold) x scale.
    float impactThreshold = 6000.0f;
    float impactDamagePerImpulse = 0.02f;
    /// Health lost per joule of RadialDamage energy (after falloff) from blasts.
    float explosionDamagePerJoule = 0.05f;
};

/// Vehicles parsed from a data file, in file order, plus non-fatal problems (each also logged).
struct VehicleDefSet
{
    std::vector<VehicleDef> vehicles;
    std::vector<std::string> warnings;
};

/// Checks the ranges: mass, extents, wheel size, suspension frequency, engine torque, gear and differential ratios,
/// health > 0; 0 <= suspensionMinLength < suspensionMaxLength; 0 < engineMinRpm < engineMaxRpm; 0 <= maxSteerDegrees
/// < 90; reverseGearRatio < 0; 0 < wheelMass with four wheels lighter than the vehicle; the rest >= 0. InvalidArgument
/// names the vehicle and field.
[[nodiscard]] Result<> Validate(const VehicleDef& vehicle);

/// Parses a vehicles file:
///
///     { "vehicles": [ { "name": "truck", "mass": 2500, "halfExtents": [1, 0.6, 2.6], "drive": "all", ... } ] }
///
/// `name` is required. Vectors are [x, y, z] arrays, `gearRatios` an array of numbers, `drive` one of "front",
/// "rear", "all". A missing field takes its default and adds a warning, as does an unknown field. Malformed JSON, a
/// wrong field type, a duplicate name or an invalid value (Validate) fails with InvalidArgument.
[[nodiscard]] Result<VehicleDefSet> ParseVehicleDefs(std::string_view json);

/// Reads and parses a vehicles file. IoError if it cannot be read.
[[nodiscard]] Result<VehicleDefSet> LoadVehicleDefs(const std::filesystem::path& path);

[[nodiscard]] const VehicleDef* FindVehicle(std::span<const VehicleDef> vehicles, std::string_view name) noexcept;

} // namespace interi::gameplay
