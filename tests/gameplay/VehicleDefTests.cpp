// SPDX-License-Identifier: MIT
#include <interi/gameplay/VehicleDef.h>

#include <gtest/gtest.h>

#include <string>

using interi::ErrorCode;
using interi::gameplay::DriveLayout;
using interi::gameplay::FindVehicle;
using interi::gameplay::LoadVehicleDefs;
using interi::gameplay::ParseVehicleDefs;
using interi::gameplay::VehicleDef;

namespace
{

constexpr const char* kComplete = R"({
  "vehicles": [
    { "name": "car", "mass": 1200, "halfExtents": [0.9, 0.4, 2.0], "centerOfMassOffset": [0, -0.3, 0.1],
      "wheelRadius": 0.35, "wheelWidth": 0.25, "wheelMass": 20, "suspensionMinLength": 0.15,
      "suspensionMaxLength": 0.45, "suspensionFrequency": 1.6, "suspensionDamping": 0.4, "maxSteerDegrees": 35,
      "drive": "front", "engineTorque": 400, "engineMinRpm": 800, "engineMaxRpm": 6500, "gearRatios": [3, 2, 1],
      "reverseGearRatio": -3.2, "differentialRatio": 3.9, "brakeTorque": 1200, "handBrakeTorque": 3000,
      "health": 800, "impactThreshold": 5000, "impactDamagePerImpulse": 0.03, "explosionDamagePerJoule": 0.06 }
  ]
})";

std::string One(const std::string& fields)
{
    return R"({ "vehicles": [ { "name": "v", )" + fields + " } ] }";
}

} // namespace

TEST(VehicleDef, Load_Validates)
{
    const auto set = ParseVehicleDefs(kComplete);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    ASSERT_EQ(set->vehicles.size(), 1u);
    const VehicleDef& car = set->vehicles[0];
    EXPECT_EQ(car.name, "car");
    EXPECT_EQ(car.mass, 1200.0f);
    EXPECT_EQ(car.halfExtents.z, 2.0f);
    EXPECT_EQ(car.centerOfMassOffset.z, 0.1f);
    EXPECT_EQ(car.drive, DriveLayout::Front);
    ASSERT_EQ(car.gearRatios.size(), 3u);
    EXPECT_EQ(car.gearRatios[2], 1.0f);
    EXPECT_EQ(car.reverseGearRatio, -3.2f);
    EXPECT_EQ(car.explosionDamagePerJoule, 0.06f);
    EXPECT_EQ(FindVehicle(set->vehicles, "car"), &set->vehicles[0]);
    EXPECT_EQ(FindVehicle(set->vehicles, "bike"), nullptr);
}

TEST(VehicleDef, MissingFields_DefaultsWithWarnings)
{
    const auto set = ParseVehicleDefs(One(R"("mass": 900, "unknownField": 1)"));
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_EQ(set->vehicles[0].mass, 900.0f);
    EXPECT_EQ(set->vehicles[0].wheelRadius, VehicleDef{}.wheelRadius);
    bool unknown = false;
    for (const std::string& warning : set->warnings)
    {
        unknown = unknown || warning.find("unknownField") != std::string::npos;
    }
    EXPECT_TRUE(unknown);
    EXPECT_GT(set->warnings.size(), 10u);
}

TEST(VehicleDef, InvalidValues_Fail)
{
    for (const char* fields : {
             R"("mass": 0)",
             R"("halfExtents": [1, 0, 1])",
             R"("halfExtents": [1, 1])",
             R"("halfExtents": "big")",
             R"("wheelMass": 1000, "mass": 2000)",
             R"("suspensionMinLength": 0.5, "suspensionMaxLength": 0.5)",
             R"("maxSteerDegrees": 90)",
             R"("engineMinRpm": 6000, "engineMaxRpm": 5000)",
             R"("gearRatios": [])",
             R"("gearRatios": [1, -1])",
             R"("gearRatios": [1, "two"])",
             R"("reverseGearRatio": 2)",
             R"("drive": "sideways")",
             R"("health": -1)",
             R"("mass": "heavy")",
             R"("impactDamagePerImpulse": -0.1)",
         })
    {
        const auto set = ParseVehicleDefs(One(fields));
        ASSERT_FALSE(set.has_value()) << fields;
        EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument) << fields;
    }
}

TEST(VehicleDef, MalformedFile_Fails)
{
    EXPECT_FALSE(ParseVehicleDefs("{").has_value());
    EXPECT_FALSE(ParseVehicleDefs(R"({"cars": []})").has_value());
    EXPECT_FALSE(ParseVehicleDefs(R"({"vehicles": [ { "mass": 1 } ]})").has_value());
    EXPECT_FALSE(ParseVehicleDefs(R"({"vehicles": [ { "name": "a" }, { "name": "a" } ]})").has_value());
    EXPECT_EQ(LoadVehicleDefs("/nonexistent/vehicles.json").error().code, ErrorCode::IoError);
}

TEST(VehicleDef, SampleFile_Loads)
{
    const auto set = LoadVehicleDefs(INTERI_TEST_VEHICLES_FILE);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    EXPECT_NE(FindVehicle(set->vehicles, "truck"), nullptr);
}
