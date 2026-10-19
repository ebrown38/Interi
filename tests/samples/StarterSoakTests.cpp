// SPDX-License-Identifier: MIT
// Soak: the starter sample played with random input for many steps (INTERI_SOAK_STEPS, default 3000; the nightly
// job raises it). Nothing may crash or trip a sanitizer, the Values must stay in range, save and load must keep
// working mid-run, and the run must not depend on the worker count.
#include <interi/game/GameSession.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/ScopedTempDir.h>
#include <interi/testing/StateHasher.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

using namespace interi;
namespace fs = std::filesystem;

namespace
{

constexpr const char* kActions[] = {"moveRight", "moveLeft", "moveUp", "moveDown", "pause", "unknownAction"};

std::uint32_t SoakSteps()
{
    const char* text = std::getenv("INTERI_SOAK_STEPS");
    const long value = text != nullptr ? std::strtol(text, nullptr, 10) : 0;
    return value > 0 ? static_cast<std::uint32_t>(value) : 3000;
}

std::int32_t Int(game::GameSession& session, const char* id)
{
    const values::Value* value = session.Values().Read(id);
    return value == nullptr ? -1 : std::get<std::int32_t>(value->Variant());
}

struct SoakResult
{
    std::uint64_t hash = 0;
    std::int32_t coins = 0;
    std::int32_t total = 0;
    std::uint32_t saves = 0;
};

// Plays `steps` steps from `seed`, pressing a random action on about every fourth step, and saves and reloads now
// and then. The player's position is hashed every step.
SoakResult Play(const fs::path& root, std::uint32_t workers, std::uint64_t seed, std::uint32_t steps)
{
    const fs::path project = root / ("project" + std::to_string(workers));
    fs::copy(fs::path(INTERI_STARTER_SOURCE_DIR) / "templates" / "coins", project, fs::copy_options::recursive);
    fs::create_directories(project / "modules" / "com.interi.starter");
    fs::copy_file(fs::path(INTERI_STARTER_SOURCE_DIR) / "module.json",
                  project / "modules" / "com.interi.starter" / "module.json");

    game::GameDesc desc;
    desc.projectDirectory = project;
    desc.modules.librarySearchPaths = {INTERI_STARTER_LIBRARY_DIR};
    desc.modules.shadowDirectory = root / ("shadow" + std::to_string(workers));
    desc.saveDirectory = root / ("user" + std::to_string(workers));
    desc.workerCount = workers;
    auto opened = game::GameSession::Open(desc);
    EXPECT_TRUE(opened.has_value()) << (opened ? "" : opened.error().message);
    SoakResult result;
    if (!opened)
    {
        return result;
    }
    game::GameSession& session = **opened;
    interi::testing::DeterministicRng rng(seed);
    interi::testing::StateHasher hasher;
    for (std::uint32_t step = 0; step < steps; ++step)
    {
        if (rng.NextInt(0, 3) == 0)
        {
            session.RaiseInputAction(kActions[rng.NextInt(0, std::size(kActions) - 1)]);
        }
        session.Step();
        if (step % 500 == 499)
        {
            EXPECT_TRUE(session.SaveToSlot(0, "soak").has_value());
            EXPECT_TRUE(session.LoadFromSlot(0).has_value());
            ++result.saves;
        }
        const std::int32_t coins = Int(session, "com.interi.starter:coins");
        EXPECT_GE(coins, 0);
        hasher.Add(coins);
        hasher.Add(Int(session, "com.interi.starter:score"));
    }
    result.coins = Int(session, "com.interi.starter:coins");
    result.total = Int(session, "com.interi.starter:total");
    result.hash = hasher.Finish();
    return result;
}

} // namespace

TEST(StarterSoak, RandomInput_StaysInRangeAndSavesKeepWorking)
{
    interi::testing::ScopedTempDir temp;
    const SoakResult result = Play(temp.Path(), 2, 1234, SoakSteps());
    EXPECT_LE(result.coins, result.total);
    EXPECT_EQ(result.saves, SoakSteps() / 500);
}

TEST(StarterSoak, SameSeed_GivesTheSameRunAtAnyWorkerCount)
{
    interi::testing::ScopedTempDir temp;
    const SoakResult one = Play(temp.Path(), 1, 99, 1000);
    const SoakResult four = Play(temp.Path(), 4, 99, 1000);
    EXPECT_EQ(one.hash, four.hash);
    EXPECT_EQ(one.coins, four.coins);
}
