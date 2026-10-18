// SPDX-License-Identifier: MIT
#include <interi/sdk/Project.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <string_view>

using namespace interi;
using namespace interi::sdk;

namespace
{

constexpr std::string_view kSample = R"({
  "version": 1,
  "name": "Coin Run",
  "modules": ["com.author.coins", "com.interi.starter"],
  "content": ["content", "shared/art"],
  "startupScene": "content/scenes/main.scene.json",
  "data": "project_data.json"
})";

} // namespace

TEST(Project, Sample_Parses)
{
    auto project = ParseProject(kSample);
    ASSERT_TRUE(project.has_value()) << project.error().message;
    EXPECT_TRUE(project->warnings.empty());
    EXPECT_EQ(project->name, "Coin Run");
    EXPECT_EQ(project->modules, (std::vector<std::string>{"com.author.coins", "com.interi.starter"}));
    EXPECT_EQ(project->moduleDirs, (std::vector<std::string>{"modules"})); // default
    EXPECT_EQ(project->content.size(), 2u);
    EXPECT_EQ(project->startupScene, "content/scenes/main.scene.json");
    EXPECT_EQ(project->data, "project_data.json");
}

TEST(Project, WriteThenParse_RoundTrips)
{
    auto project = ParseProject(kSample);
    ASSERT_TRUE(project.has_value());
    const std::string text = WriteProject(*project);
    auto again = ParseProject(text);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(*again, *project);
    EXPECT_EQ(WriteProject(*again), text);
}

TEST(Project, SaveThenLoad_RecordsDirectory)
{
    interi::testing::ScopedTempDir temp;
    Project project;
    project.name = "Saved";
    project.modules = {"com.test.a"};
    project.directory = temp.Path() / "game";
    ASSERT_TRUE(SaveProject(project).has_value());
    auto loaded = LoadProject(project.directory);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_EQ(*loaded, project);
    EXPECT_EQ(loaded->directory, project.directory);
    EXPECT_FALSE(LoadProject(temp.Path() / "missing").has_value());
}

TEST(Project, MissingNameAndUnknownKeys_Warn)
{
    auto project = ParseProject(R"({"version": 1, "extra": true})");
    ASSERT_TRUE(project.has_value());
    EXPECT_EQ(project->warnings.size(), 2u);
}

TEST(Project, Malformed_ErrorsNameTheField)
{
    struct Case
    {
        std::string_view json;
        std::string_view message;
    };
    const Case cases[] = {
        {"1", "not a JSON object"},
        {R"({"name": "x"})", "\"version\""},
        {R"({"version": 1, "name": 3})", "\"name\""},
        {R"({"version": 1, "modules": "a"})", "\"modules\" must be an array"},
        {R"({"version": 1, "modules": ["a", "a"]})", "lists 'a' twice"},
        {R"({"version": 1, "content": ["../outside"]})", "\"content\"[0]"},
        {R"({"version": 1, "content": ["/abs"]})", "\"content\"[0]"},
        {R"({"version": 1, "startupScene": "C:/x.scene.json"})", "\"startupScene\""},
        {R"({"version": 1, "data": "a//b"})", "\"data\""},
    };
    for (const Case& c : cases)
    {
        auto project = ParseProject(c.json);
        ASSERT_FALSE(project.has_value()) << c.json;
        EXPECT_NE(project.error().message.find(c.message), std::string::npos) << project.error().message;
    }
    auto newer = ParseProject(R"({"version": 2})");
    ASSERT_FALSE(newer.has_value());
    EXPECT_EQ(newer.error().code, ErrorCode::VersionMismatch);
}
