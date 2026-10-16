// SPDX-License-Identifier: MIT
#include <interi/destruction_audio/AudioConfig.h>

#include <gtest/gtest.h>

#include <string>

using interi::ErrorCode;
using interi::StringId;
using interi::destruction::ImpactBank;
using interi::destruction::ImpactVolume;
using interi::destruction::LoadDestructionAudioConfig;
using interi::destruction::ParseDestructionAudioConfig;
using interi::destruction::RumbleCue;
using interi::destruction::RumbleVolume;
using interi::destruction::SelectImpactBank;

namespace
{

ImpactBank Bank(const char* a, const char* b)
{
    ImpactBank bank;
    bank.materialA = a;
    bank.materialB = b;
    bank.idA = bank.materialA == "*" ? StringId() : StringId(a);
    bank.idB = bank.materialB == "*" ? StringId() : StringId(b);
    return bank;
}

std::string Config(const std::string& impacts)
{
    return R"({ "impacts": [ )" + impacts + R"( ],
      "creak": { "clips": ["c"] }, "rumble": { "clips": ["r"] }, "explosion": { "clips": ["e"] } })";
}

} // namespace

TEST(ImpactAudio, MaterialPair_SelectsBank)
{
    const std::vector<ImpactBank> banks = {Bank("*", "*"), Bank("concrete", "*"), Bank("steel", "concrete"),
                                           Bank("concrete", "concrete"), Bank("glass", "*")};
    const StringId concrete("concrete");
    const StringId steel("steel");
    const StringId glass("glass");
    EXPECT_EQ(SelectImpactBank(banks, concrete, concrete), 3u); // exact pair
    EXPECT_EQ(SelectImpactBank(banks, steel, concrete), 2u);
    EXPECT_EQ(SelectImpactBank(banks, concrete, steel), 2u); // either order
    EXPECT_EQ(SelectImpactBank(banks, glass, steel), 4u);    // one wildcard
    EXPECT_EQ(SelectImpactBank(banks, steel, glass), 4u);
}

TEST(ImpactAudio, Wildcard_FallsBack)
{
    const std::vector<ImpactBank> banks = {Bank("*", "*"), Bank("wood", "*")};
    EXPECT_EQ(SelectImpactBank(banks, StringId("dirt"), StringId("rubber")), 0u);
    EXPECT_EQ(SelectImpactBank(banks, StringId(), StringId()), 0u); // unnamed materials match only "*"
    EXPECT_EQ(SelectImpactBank(banks, StringId(), StringId("wood")), 1u);
    const std::vector<ImpactBank> noWildcard = {Bank("wood", "wood")};
    EXPECT_FALSE(SelectImpactBank(noWildcard, StringId("wood"), StringId("glass")).has_value());
    EXPECT_FALSE(SelectImpactBank({}, StringId("wood"), StringId("wood")).has_value());
}

TEST(ImpactAudio, Volume_RisesWithImpulse)
{
    ImpactBank bank;
    bank.minImpulse = 100.0f;
    bank.fullImpulse = 10000.0f;
    bank.minVolume = 0.2f;
    bank.volume = 0.5f;
    EXPECT_EQ(ImpactVolume(bank, 50.0f), 0.0f);
    EXPECT_NEAR(ImpactVolume(bank, 100.0f), 0.5f * 0.2f, 1e-6f);
    EXPECT_NEAR(ImpactVolume(bank, 1000.0f), 0.5f * (0.2f + 0.8f * 0.5f), 1e-5f); // halfway in log
    EXPECT_NEAR(ImpactVolume(bank, 10000.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(ImpactVolume(bank, 1.0e6f), 0.5f, 1e-6f);
}

TEST(CollapseRumble, ScalesWithMass)
{
    RumbleCue cue;
    cue.minMass = 1000.0f;
    cue.fullMass = 100000.0f;
    EXPECT_EQ(RumbleVolume(cue, 500.0f), 0.0f);
    float previous = 0.0f;
    for (const float mass : {1500.0f, 5000.0f, 20000.0f, 60000.0f})
    {
        const float volume = RumbleVolume(cue, mass);
        EXPECT_GT(volume, previous) << mass;
        previous = volume;
    }
    EXPECT_NEAR(RumbleVolume(cue, 10000.0f), 0.5f, 1e-5f);
    EXPECT_NEAR(RumbleVolume(cue, 1.0e7f), 1.0f, 1e-6f);
}

TEST(ImpactAudio, Config_Parses)
{
    const auto set = ParseDestructionAudioConfig(Config(
        R"({ "materials": ["steel", "*"], "clips": ["a", "b"], "minImpulse": 10, "fullImpulse": 100,
             "minVolume": 0.5, "volume": 0.9, "priority": 0.4, "minDistance": 1, "maxDistance": 20, "rolloff": 2 })"));
    ASSERT_TRUE(set.has_value()) << set.error().message;
    ASSERT_EQ(set->config.impacts.size(), 1u);
    const ImpactBank& bank = set->config.impacts[0];
    EXPECT_EQ(bank.idA, StringId("steel"));
    EXPECT_FALSE(bank.idB.IsValid());
    EXPECT_EQ(bank.clips.size(), 2u);
    EXPECT_EQ(bank.attenuation.maxDistance, 20.0f);
    EXPECT_EQ(set->config.creak.clips[0], "c");
    EXPECT_FALSE(set->warnings.empty()); // the creak, rumble and explosion numbers are missing
}

TEST(ImpactAudio, Config_InvalidFails)
{
    for (const char* impacts : {
             R"({ "materials": ["a"], "clips": ["x"] })",
             R"({ "materials": ["a", "b"], "clips": [] })",
             R"({ "materials": ["a", "b"], "clips": [3] })",
             R"({ "materials": ["a", "b"], "clips": ["x"], "minImpulse": 10, "fullImpulse": 5 })",
             R"({ "materials": ["a", "b"], "clips": ["x"], "minVolume": 2 })",
             R"({ "materials": ["a", "b"], "clips": ["x"], "minDistance": 10, "maxDistance": 5 })",
             R"({ "materials": ["a", "b"], "clips": ["x"], "volume": "loud" })",
         })
    {
        const auto set = ParseDestructionAudioConfig(Config(impacts));
        ASSERT_FALSE(set.has_value()) << impacts;
        EXPECT_EQ(set.error().code, ErrorCode::InvalidArgument) << impacts;
    }
    EXPECT_FALSE(ParseDestructionAudioConfig("[").has_value());
    EXPECT_FALSE(ParseDestructionAudioConfig(R"({ "impacts": [] })").has_value()); // no creak/rumble/explosion
}

TEST(ImpactAudio, SampleFile_Loads)
{
    const auto set = LoadDestructionAudioConfig(INTERI_TEST_DESTRUCTION_AUDIO_FILE);
    ASSERT_TRUE(set.has_value()) << set.error().message;
    EXPECT_TRUE(set->warnings.empty());
    EXPECT_GE(set->config.impacts.size(), 5u);
    EXPECT_EQ(LoadDestructionAudioConfig("/nonexistent.json").error().code, ErrorCode::IoError);
}
