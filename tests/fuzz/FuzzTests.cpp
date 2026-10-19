// SPDX-License-Identifier: MIT
// Mutation fuzzing of the text parsers that read files a user or a module author wrote: module.json, project.json,
// chains, expressions, scenes and HUD documents. Valid documents (the starter sample's) are mutated with a seeded
// generator; every mutant must parse or fail with an error, never crash, hang or trip a sanitizer. A parse that
// succeeds must also survive its own writer (write, parse, write is a fixed point).
//
// INTERI_FUZZ_ITERATIONS scales the mutants per document (the nightly job raises it); INTERI_TEST_SEED the seed.
#include <interi/chain/ChainAsset.h>
#include <interi/chain/Expression.h>
#include <interi/core/Random.h>
#include <interi/hud/HudDocument.h>
#include <interi/scene/SceneFile.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Project.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/Environment.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace interi;
namespace fs = std::filesystem;

namespace
{

const fs::path kStarter = INTERI_STARTER_SOURCE_DIR;

std::string ReadText(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << path;
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<fs::path> FilesWithSuffix(const fs::path& root, std::string_view suffix)
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (entry.is_regular_file() && entry.path().string().ends_with(suffix))
        {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::uint32_t Iterations()
{
    return interi::testing::EnvUint("INTERI_FUZZ_ITERATIONS", 400);
}

std::uint32_t Below(interi::testing::DeterministicRng& rng, std::uint32_t bound)
{
    return static_cast<std::uint32_t>(rng.NextInt(0, static_cast<std::int64_t>(bound) - 1));
}

// One random edit of `text`: flip a byte, delete or duplicate a span, truncate, insert JSON punctuation or a
// number/keyword that stresses the readers, or splice in a span from elsewhere in the same text.
std::string Mutate(std::string text, interi::testing::DeterministicRng& rng)
{
    static constexpr std::string_view kTokens[] = {"{",       "}",
                                                   "[",       "]",
                                                   "\"",      ",",
                                                   ":",       "null",
                                                   "true",    "-1",
                                                   "1e999",   "-0.0",
                                                   "NaN",     "4294967296",
                                                   "\\u0000", "\\ud800",
                                                   "\"\"",    "[]",
                                                   "{}",      "\\",
                                                   "1.5",     "99999999999999999999",
                                                   "\"..\"",  "\"/\"",
                                                   "(",       ")",
                                                   "\xff\xfe"};
    const std::uint32_t edits = 1 + Below(rng, 4);
    for (std::uint32_t i = 0; i < edits; ++i)
    {
        if (text.empty())
        {
            text = "{}";
        }
        const auto at = static_cast<std::size_t>(Below(rng, static_cast<std::uint32_t>(text.size())));
        const auto length = static_cast<std::size_t>(1 + Below(rng, 16));
        switch (Below(rng, 7))
        {
        case 0:
            text[at] = static_cast<char>(Below(rng, 256));
            break;
        case 1:
            text.erase(at, length);
            break;
        case 2:
            text.insert(at, text.substr(at, length));
            break;
        case 3:
            text.resize(at);
            break;
        case 4:
            text.insert(at, kTokens[Below(rng, static_cast<std::uint32_t>(std::size(kTokens)))]);
            break;
        case 5:
        {
            const auto from = static_cast<std::size_t>(Below(rng, static_cast<std::uint32_t>(text.size())));
            text.insert(at, text.substr(from, length));
            break;
        }
        default:
            text[at] = static_cast<char>(text[at] ^ (1 << Below(rng, 8)));
            break;
        }
    }
    return text;
}

// Runs `parse` over the document and its mutants; `parse` returns true when the text parsed.
class FuzzTest : public interi::testing::SeededTest
{
protected:
    void Fuzz(const std::string& name, const std::string& document,
              const std::function<bool(const std::string&)>& parse)
    {
        EXPECT_TRUE(parse(document)) << name << " (the unmutated document) must parse";
        std::uint32_t accepted = 0;
        const std::uint32_t iterations = Iterations();
        for (std::uint32_t i = 0; i < iterations; ++i)
        {
            accepted += parse(Mutate(document, Rng())) ? 1u : 0u;
        }
        // Not an assertion on the count: a mutant may be valid. Surviving all of them is the test.
        RecordProperty(name + "_accepted", static_cast<int>(accepted));
    }
};

} // namespace

TEST_F(FuzzTest, Manifest_MutantsNeverCrash)
{
    const std::string text = ReadText(kStarter / "module.json");
    Fuzz("module.json", text, [](const std::string& json) { return sdk::ParseManifest(json).has_value(); });
}

TEST_F(FuzzTest, Project_MutantsNeverCrashAndRoundTrip)
{
    const std::string text = ReadText(kStarter / "templates/coins/project.json");
    Fuzz("project.json", text,
         [](const std::string& json)
         {
             auto project = sdk::ParseProject(json);
             if (!project)
             {
                 return false;
             }
             const std::string written = sdk::WriteProject(*project);
             auto again = sdk::ParseProject(written);
             EXPECT_TRUE(again.has_value()) << written;
             if (again)
             {
                 EXPECT_EQ(sdk::WriteProject(*again), written);
             }
             return true;
         });
}

TEST_F(FuzzTest, Chains_MutantsNeverCrashAndRoundTrip)
{
    const auto chains = FilesWithSuffix(kStarter / "templates/coins", ".chain.json");
    ASSERT_FALSE(chains.empty());
    for (const fs::path& path : chains)
    {
        Fuzz(path.filename().string(), ReadText(path),
             [](const std::string& json)
             {
                 auto chain = chain::ParseChain(json);
                 if (!chain)
                 {
                     return false;
                 }
                 const std::string written = chain::WriteChain(*chain);
                 auto again = chain::ParseChain(written);
                 EXPECT_TRUE(again.has_value()) << written;
                 if (again)
                 {
                     EXPECT_EQ(chain::WriteChain(*again), written);
                 }
                 return true;
             });
    }
}

TEST_F(FuzzTest, Scenes_MutantsNeverCrashAndRoundTrip)
{
    const auto scenes = FilesWithSuffix(kStarter / "templates/coins", ".scene.json");
    ASSERT_FALSE(scenes.empty());
    for (const fs::path& path : scenes)
    {
        Fuzz(path.filename().string(), ReadText(path),
             [](const std::string& json)
             {
                 auto scene = scene::ParseScene(json);
                 if (!scene)
                 {
                     return false;
                 }
                 const std::string written = scene::WriteScene(*scene);
                 auto again = scene::ParseScene(written);
                 EXPECT_TRUE(again.has_value()) << written;
                 if (again)
                 {
                     EXPECT_EQ(scene::WriteScene(*again), written);
                 }
                 return true;
             });
    }
}

TEST_F(FuzzTest, HudDocuments_MutantsNeverCrash)
{
    const auto huds = FilesWithSuffix(kStarter / "templates/coins", ".hud.json");
    ASSERT_FALSE(huds.empty());
    for (const fs::path& path : huds)
    {
        Fuzz(path.filename().string(), ReadText(path),
             [](const std::string& json) { return hud::ParseHud(json).has_value(); });
    }
}

TEST_F(FuzzTest, Expressions_MutantsCompileOrFailWithASpan)
{
    const std::string expressions[] = {
        "value(\"com.test.a:Armor\") * 2 + min(1, 2)",
        "clamp(payload.damage - 3, 0, 100) / max(1, 2)",
        "vec3(1.0, 2.0, 3.0) == vec3(1.0, 2.0, 3.0) && !(1 < 2) || length(vec3(0.0, 1.0, 0.0)) > 0.5",
        "int(sqrt(abs(-16.0))) % 3 + floor(2.7)",
        "dot(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0))",
    };
    chain::ExprEnvironment environment;
    for (const std::string& expression : expressions)
    {
        for (std::uint32_t i = 0; i < Iterations(); ++i)
        {
            const std::string mutant = i == 0 ? expression : Mutate(expression, Rng());
            auto compiled = chain::CompileExpression(mutant, environment);
            if (!compiled)
            {
                EXPECT_LE(compiled.error().span.begin, mutant.size()) << mutant;
            }
            else
            {
                (void)chain::EvaluateExpression(*compiled, {});
            }
        }
    }
}

TEST_F(FuzzTest, DeeplyNestedInput_IsRejectedWithoutExhaustingTheStack)
{
    const std::string deepArray(200000, '[');
    const std::string deepObject = []
    {
        std::string text;
        for (int i = 0; i < 100000; ++i)
        {
            text += "{\"a\":";
        }
        return text;
    }();
    const std::string deepParens = std::string(200000, '(') + "1" + std::string(200000, ')');
    EXPECT_FALSE(sdk::ParseManifest(deepArray).has_value());
    EXPECT_FALSE(sdk::ParseManifest(deepObject).has_value());
    EXPECT_FALSE(chain::ParseChain(deepObject).has_value());
    EXPECT_FALSE(scene::ParseScene(deepArray).has_value());
    EXPECT_FALSE(sdk::ParseProject(deepObject).has_value());
    EXPECT_FALSE(hud::ParseHud(deepObject).has_value());
    EXPECT_FALSE(chain::CompileExpression(deepParens, {}).has_value());
    EXPECT_FALSE(chain::CompileExpression(std::string(200000, '-') + "1", {}).has_value());

    // The evaluator recurses over the tree, so a long flat chain (`1+1+...` leans left) is as deep as nested
    // parentheses: one at the depth limit compiles and evaluates, one past it and a huge one are refused.
    const auto flatSum = [](std::uint32_t depth)
    {
        std::string text = "1";
        for (std::uint32_t i = 1; i < depth; ++i)
        {
            text += "+1";
        }
        return text;
    };
    EXPECT_FALSE(chain::CompileExpression(flatSum(50000), {}).has_value());
    EXPECT_FALSE(chain::CompileExpression(flatSum(chain::kMaxExpressionDepth + 1), {}).has_value());
    auto compiled = chain::CompileExpression(flatSum(chain::kMaxExpressionDepth), {});
    ASSERT_TRUE(compiled.has_value());
    (void)chain::EvaluateExpression(*compiled, {});
}
