// SPDX-License-Identifier: MIT
#include <interi/gameplay/WeaponDef.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using interi::ErrorCode;
using interi::gameplay::FindWeapon;
using interi::gameplay::ParseWeaponDefs;
using interi::gameplay::WeaponKind;

namespace
{

constexpr const char* kComplete = R"({
  "weapons": [
    { "name": "hammer", "kind": "melee", "energy": 400, "innerRadius": 0.3, "radius": 0.8, "impulse": 600,
      "cooldown": 0.6, "range": 2.2, "hitRadius": 0.25 },
    { "name": "charge", "kind": "remoteCharge", "energy": 20000, "innerRadius": 1, "radius": 3, "impulse": 3000,
      "cooldown": 0.3, "range": 4, "maxCharges": 6 },
    { "name": "rocket", "kind": "rocket", "energy": 8000, "innerRadius": 0.8, "radius": 2.5, "impulse": 2000,
      "cooldown": 1, "range": 120, "speed": 40, "hitRadius": 0.1 }
  ]
})";

std::string One(const std::string& fields)
{
    return R"({ "weapons": [ { "name": "w", )" + fields + " } ] }";
}

} // namespace

TEST(WeaponDef, Load_Validates)
{
    const auto set = ParseWeaponDefs(kComplete);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    ASSERT_EQ(set->weapons.size(), 3u);
    EXPECT_EQ(set->weapons[0].kind, WeaponKind::Melee);
    EXPECT_EQ(set->weapons[0].hitRadius, 0.25f);
    EXPECT_EQ(set->weapons[1].kind, WeaponKind::RemoteCharge);
    EXPECT_EQ(set->weapons[1].maxCharges, 6u);
    EXPECT_EQ(set->weapons[2].kind, WeaponKind::Rocket);
    EXPECT_EQ(set->weapons[2].speed, 40.0f);
    EXPECT_EQ(FindWeapon(set->weapons, "rocket"), &set->weapons[2]);
    EXPECT_EQ(FindWeapon(set->weapons, "nope"), nullptr);
}

TEST(WeaponDef, ShippedFile_Loads)
{
    const auto set = interi::gameplay::LoadWeaponDefs(INTERI_TEST_WEAPONS_FILE);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    EXPECT_NE(FindWeapon(set->weapons, "sledgehammer"), nullptr);
    EXPECT_NE(FindWeapon(set->weapons, "remote_charge"), nullptr);
    EXPECT_NE(FindWeapon(set->weapons, "rocket"), nullptr);
}

TEST(WeaponDef, MissingField_WarnsAndDefaults)
{
    const auto set = ParseWeaponDefs(One(R"("kind": "melee", "energy": 10, "radius": 2, "bogus": 1)"));
    ASSERT_TRUE(set.has_value()) << set.error().message;
    const auto& warnings = set->warnings;
    const auto mentions = [&](const std::string& text)
    { return std::ranges::any_of(warnings, [&](const std::string& w) { return w.find(text) != std::string::npos; }); };
    EXPECT_TRUE(mentions("\"range\""));
    EXPECT_TRUE(mentions("\"bogus\""));
    EXPECT_FALSE(mentions("\"speed\"")); // rockets only
    EXPECT_EQ(set->weapons[0].range, interi::gameplay::WeaponDef{}.range);
}

TEST(WeaponDef, Invalid_Fails)
{
    const char* const cases[] = {
        R"("kind": "laser")",                                // unknown kind
        R"("energy": 1)",                                    // no kind
        R"("kind": "melee", "radius": "far")",               // wrong type
        R"("kind": "melee", "innerRadius": 2, "radius": 1)", // radius <= innerRadius
        R"("kind": "melee", "energy": -1)",                  // negative energy
        R"("kind": "rocket", "speed": 0)",                   // a rocket must move
        R"("kind": "remoteCharge", "maxCharges": 0)",        // no charges
        R"("kind": "melee", "range": 0)",                    // no reach
        R"("kind": "remoteCharge", "maxCharges": -2)",       // not unsigned
    };
    for (const char* fields : cases)
    {
        const auto set = ParseWeaponDefs(One(fields));
        ASSERT_FALSE(set.has_value()) << fields;
        EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument) << fields;
    }
}

TEST(WeaponDef, DuplicateNameOrBadJson_Fails)
{
    EXPECT_FALSE(ParseWeaponDefs(R"({ "weapons": [ { "name": "a", "kind": "melee" },
                                                    { "name": "a", "kind": "rocket", "speed": 1 } ] })")
                     .has_value());
    EXPECT_FALSE(ParseWeaponDefs("{ not json").has_value());
    EXPECT_FALSE(ParseWeaponDefs(R"({ "guns": [] })").has_value());
    EXPECT_EQ(interi::gameplay::LoadWeaponDefs("does/not/exist.json").error().code, ErrorCode::IoError);
}
