// SPDX-License-Identifier: MIT
#include <interi/chain/ChainAsset.h>
#include <interi/platform/FileSystem.h>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace interi;
using namespace interi::chain;

namespace
{

constexpr std::string_view kSample = R"({
  "version": 1,
  "priority": 3,
  "nodes": [
    { "id": "fire", "event": "trigger.InputAction", "params": { "key": { "const": "input.Fire" } } },
    { "id": "shoot", "event": "com.test.a:FireGun", "params": { "range": { "value": "com.test.a:Range" } } },
    { "id": "hurt", "event": "world.ModifyValue",
      "params": { "id": { "const": "com.test.a:Health" }, "delta": { "expr": "-payload.damage * 2" },
                  "who": { "payload": "target" } } }
  ],
  "links": [
    { "from": "fire", "to": "shoot" },
    { "from": "shoot.Hit", "to": "hurt", "condition": "payload.damage > 0" }
  ]
})";

} // namespace

TEST(ChainAsset, Sample_Parses)
{
    auto asset = ParseChain(kSample);
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_TRUE(asset->warnings.empty());
    EXPECT_EQ(asset->priority, 3);
    ASSERT_EQ(asset->nodes.size(), 3u);
    EXPECT_EQ(asset->nodes[1].event, "com.test.a:FireGun");
    ASSERT_EQ(asset->nodes[2].params.size(), 3u);
    EXPECT_EQ(asset->nodes[2].params[0].first, "delta");
    EXPECT_EQ(asset->nodes[2].params[0].second,
              (Binding{.kind = Binding::Kind::Expression, .text = "-payload.damage * 2"}));
    EXPECT_EQ(asset->nodes[2].params[1].second,
              (Binding{.kind = Binding::Kind::Constant, .text = "\"com.test.a:Health\""}));
    EXPECT_EQ(asset->nodes[2].params[2].second, (Binding{.kind = Binding::Kind::Payload, .text = "target"}));
    ASSERT_EQ(asset->links.size(), 2u);
    EXPECT_EQ(asset->links[0].fromOutput, "");
    EXPECT_EQ(asset->links[1].fromNode, "shoot");
    EXPECT_EQ(asset->links[1].fromOutput, "Hit");
    EXPECT_EQ(asset->links[1].condition, "payload.damage > 0");
}

TEST(ChainAsset, WriteThenParse_RoundTrips)
{
    auto asset = ParseChain(kSample);
    ASSERT_TRUE(asset.has_value());
    const std::string written = WriteChain(*asset);
    auto again = ParseChain(written);
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_EQ(*again, *asset);
    EXPECT_EQ(WriteChain(*again), written);
}

TEST(ChainAsset, MalformedNode_ErrorNamesIndex)
{
    auto missingEvent = ParseChain(R"({"version": 1, "nodes": [{"id": "a", "event": "x"}, {"id": "b"}]})");
    ASSERT_FALSE(missingEvent.has_value());
    EXPECT_NE(missingEvent.error().message.find("node 1"), std::string::npos);

    auto badId = ParseChain(R"({"version": 1, "nodes": [{"id": "a.b", "event": "x"}]})");
    ASSERT_FALSE(badId.has_value());

    auto duplicate = ParseChain(R"({"version": 1, "nodes": [{"id": "a", "event": "x"}, {"id": "a", "event": "y"}]})");
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_NE(duplicate.error().message.find("duplicate"), std::string::npos);

    auto badBinding =
        ParseChain(R"({"version": 1, "nodes": [{"id": "a", "event": "x", "params": {"p": {"script": "1"}}}]})");
    ASSERT_FALSE(badBinding.has_value());
    EXPECT_NE(badBinding.error().message.find("param 'p'"), std::string::npos);
}

TEST(ChainAsset, LinkToMissingNode_Error)
{
    auto asset =
        ParseChain(R"({"version": 1, "nodes": [{"id": "a", "event": "x"}], "links": [{"from": "a", "to": "b"}]})");
    ASSERT_FALSE(asset.has_value());
    EXPECT_NE(asset.error().message.find("link 0"), std::string::npos);
    EXPECT_NE(asset.error().message.find("'b'"), std::string::npos);
}

TEST(ChainAsset, UnknownKeys_Warn)
{
    auto asset =
        ParseChain(R"({"version": 1, "colour": "red", "nodes": [{"id": "a", "event": "x", "zorder": [1, 2]}]})");
    ASSERT_TRUE(asset.has_value());
    EXPECT_EQ(asset->warnings.size(), 2u);
}

TEST(ChainAsset, NewerVersion_Rejected)
{
    auto asset = ParseChain(R"({"version": 99})");
    ASSERT_FALSE(asset.has_value());
    EXPECT_EQ(asset.error().code, ErrorCode::VersionMismatch);
}

TEST(ChainAsset, LoadChain_ReadsFromFileSystem)
{
    MemoryFileSystem files;
    const std::string text(kSample);
    ASSERT_TRUE(files.WriteFile("chains/fire.chain.json", std::as_bytes(std::span(text))).has_value());
    auto asset = LoadChain(files, "chains/fire.chain.json");
    ASSERT_TRUE(asset.has_value()) << asset.error().message;
    EXPECT_EQ(asset->nodes.size(), 3u);
    EXPECT_FALSE(LoadChain(files, "chains/missing.chain.json").has_value());
}

TEST(ChainAsset, EditorData_RoundTrips_AndStaysOutOfPlainFiles)
{
    auto plain = ParseChain(kSample);
    ASSERT_TRUE(plain.has_value());
    const std::string text = WriteChain(*plain);
    EXPECT_EQ(text.find("editor"), std::string::npos); // nothing written when empty
    EXPECT_EQ(text.find("pos"), std::string::npos);

    ChainAsset asset = *plain;
    asset.nodes[0].pos = {10.0f, -20.5f};
    asset.comments.push_back({.id = "note", .text = "hello", .pos = {1.0f, 2.0f}, .size = {30.0f, 40.0f}});
    asset.groups.push_back({.id = "g", .title = "Group", .nodes = {"fire", "shoot"}, .color = {1, 0, 0, 1}});
    auto again = ParseChain(WriteChain(asset));
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_TRUE(again->warnings.empty());
    EXPECT_EQ(*again, asset);
    EXPECT_EQ(WriteChain(*again), WriteChain(asset));
}

TEST(ChainAsset, EditorData_Malformed_Rejected)
{
    EXPECT_FALSE(ParseChain(R"({"version": 1, "nodes": [{"id": "a", "event": "x", "pos": [1]}]})").has_value());
    EXPECT_FALSE(ParseChain(R"({"version": 1, "editor": {"comments": [{"id": "a b"}]}})").has_value());
    EXPECT_FALSE(ParseChain(R"({"version": 1, "editor": {"groups": [{"id": "g", "nodes": [1]}]}})").has_value());
}
