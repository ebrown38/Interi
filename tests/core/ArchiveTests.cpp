// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr interi::FourCC kTestMagic = interi::MakeFourCC("TEST");

enum class Material : std::uint8_t
{
    Concrete = 3,
    Steel = 7,
};

/// Version history: v1 = {mass, name}; v2 adds `strength` (defaults to 1.0 for v1 data).
struct ChunkDesc
{
    float mass = 0.0f;
    std::string name;
    float strength = 1.0f;
};

template <typename Archive>
void Serialize(Archive& archive, ChunkDesc& chunk)
{
    archive(chunk.mass, chunk.name);
    if (archive.Version() >= 2)
    {
        archive(chunk.strength);
    }
}

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (int v : values)
    {
        bytes.push_back(static_cast<std::byte>(v));
    }
    return bytes;
}
} // namespace

TEST(Archive, RoundTrip_AllPrimitiveTypes)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 1);
        bool b = true;
        std::uint8_t u8 = 0xAB;
        std::int8_t i8 = -5;
        std::uint16_t u16 = 0xBEEF;
        std::int16_t i16 = -12345;
        std::uint32_t u32 = 0xDEADBEEF;
        std::int32_t i32 = std::numeric_limits<std::int32_t>::min();
        std::uint64_t u64 = 0x0123'4567'89AB'CDEFull;
        std::int64_t i64 = -1;
        float f32 = -0.0f;
        double f64 = 3.141592653589793;
        Material material = Material::Steel;
        std::string text = "rebar";
        std::vector<std::int32_t> list{1, -2, 3};
        writer(b, u8, i8, u16, i16, u32, i32, u64, i64, f32, f64, material, text, list);
    }

    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 1);
    ASSERT_TRUE(reader.has_value()) << reader.error().message;
    bool b = false;
    std::uint8_t u8 = 0;
    std::int8_t i8 = 0;
    std::uint16_t u16 = 0;
    std::int16_t i16 = 0;
    std::uint32_t u32 = 0;
    std::int32_t i32 = 0;
    std::uint64_t u64 = 0;
    std::int64_t i64 = 0;
    float f32 = 1.0f;
    double f64 = 0.0;
    Material material = Material::Concrete;
    std::string text;
    std::vector<std::int32_t> list;
    (*reader)(b, u8, i8, u16, i16, u32, i32, u64, i64, f32, f64, material, text, list);
    ASSERT_TRUE(reader->Finish().has_value());

    EXPECT_TRUE(b);
    EXPECT_EQ(u8, 0xAB);
    EXPECT_EQ(i8, -5);
    EXPECT_EQ(u16, 0xBEEF);
    EXPECT_EQ(i16, -12345);
    EXPECT_EQ(u32, 0xDEADBEEFu);
    EXPECT_EQ(i32, std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(u64, 0x0123'4567'89AB'CDEFull);
    EXPECT_EQ(i64, -1);
    EXPECT_EQ(f32, 0.0f);
    EXPECT_TRUE(std::signbit(f32)) << "bit-exact, including negative zero";
    EXPECT_EQ(f64, 3.141592653589793);
    EXPECT_EQ(material, Material::Steel);
    EXPECT_EQ(text, "rebar");
    EXPECT_EQ(list, (std::vector<std::int32_t>{1, -2, 3}));
}

TEST(Archive, ByteLayout_IsLittleEndian)
{
    std::vector<std::byte> buffer;
    interi::ArchiveWriter writer(buffer, kTestMagic, 9);
    std::uint32_t value = 0x01020304;
    writer(value);
    // magic "TEST", version 9, value.
    EXPECT_EQ(buffer, Bytes({'T', 'E', 'S', 'T', 9, 0, 0, 0, 4, 3, 2, 1}));
}

TEST(Archive, OlderVersion_ReadsWithDefaults)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 1); // pretend an older build wrote this
        ChunkDesc old{.mass = 12.5f, .name = "pillar", .strength = 99.0f};
        writer(old); // v1 layout: strength is not written
    }
    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 2);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader->Version(), 1u);
    ChunkDesc loaded;
    (*reader)(loaded);
    ASSERT_TRUE(reader->Finish().has_value());
    EXPECT_EQ(loaded.mass, 12.5f);
    EXPECT_EQ(loaded.name, "pillar");
    EXPECT_EQ(loaded.strength, 1.0f) << "field added in v2 keeps its default";
}

TEST(Archive, CurrentVersion_RoundTripsNewFields)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 2);
        ChunkDesc chunk{.mass = 1.0f, .name = "beam", .strength = 4.0f};
        writer(chunk);
    }
    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 2);
    ASSERT_TRUE(reader.has_value());
    ChunkDesc loaded;
    (*reader)(loaded);
    EXPECT_EQ(loaded.strength, 4.0f);
}

TEST(Archive, NewerVersion_ReturnsVersionMismatch)
{
    std::vector<std::byte> buffer;
    interi::ArchiveWriter writer(buffer, kTestMagic, 3);
    const auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 2);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(reader.error().code, interi::ErrorCode::VersionMismatch);
}

TEST(Archive, WrongMagic_ReturnsCorrupt)
{
    std::vector<std::byte> buffer;
    interi::ArchiveWriter writer(buffer, interi::MakeFourCC("MESH"), 1);
    const auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 1);
    ASSERT_FALSE(reader.has_value());
    EXPECT_EQ(reader.error().code, interi::ErrorCode::Corrupt);
}

TEST(Archive, TruncatedData_FailsWithoutReadingPastEnd)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 1);
        std::uint64_t value = 42;
        writer(value);
    }
    buffer.resize(buffer.size() - 3);
    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 1);
    ASSERT_TRUE(reader.has_value());
    std::uint64_t value = 7;
    (*reader)(value);
    EXPECT_FALSE(reader->Ok());
    EXPECT_EQ(value, 0u) << "failed reads produce zero, never garbage";
    const auto finished = reader->Finish();
    ASSERT_FALSE(finished.has_value());
    EXPECT_EQ(finished.error().code, interi::ErrorCode::Corrupt);
}

TEST(Archive, CorruptLength_IsRejectedBeforeAllocating)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 1);
        std::uint32_t hugeCount = 0xFFFF'FFF0u; // looks like a vector/string length
        writer(hugeCount);
    }
    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 1);
    ASSERT_TRUE(reader.has_value());
    std::string text;
    (*reader)(text);
    EXPECT_FALSE(reader->Ok());
    EXPECT_TRUE(text.empty());
}

TEST(Archive, TrailingBytes_AreReportedByFinish)
{
    std::vector<std::byte> buffer;
    {
        interi::ArchiveWriter writer(buffer, kTestMagic, 1);
        std::uint32_t a = 1;
        std::uint32_t b = 2;
        writer(a, b);
    }
    auto reader = interi::ArchiveReader::Open(buffer, kTestMagic, 1);
    ASSERT_TRUE(reader.has_value());
    std::uint32_t a = 0;
    (*reader)(a);
    EXPECT_TRUE(reader->Ok());
    EXPECT_FALSE(reader->Finish().has_value()) << "unread data means the reader and writer disagree on layout";
}
