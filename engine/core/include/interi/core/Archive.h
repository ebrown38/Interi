// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Assert.h>
#include <interi/core/Result.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace interi
{

/// Four-character file tag. MakeFourCC("MESH") serializes as the bytes 'M','E','S','H'.
using FourCC = std::uint32_t;

[[nodiscard]] constexpr FourCC MakeFourCC(const char (&text)[5]) noexcept
{
    return static_cast<FourCC>(static_cast<unsigned char>(text[0])) |
           (static_cast<FourCC>(static_cast<unsigned char>(text[1])) << 8) |
           (static_cast<FourCC>(static_cast<unsigned char>(text[2])) << 16) |
           (static_cast<FourCC>(static_cast<unsigned char>(text[3])) << 24);
}

namespace detail
{
template <typename T>
concept ArchivePrimitive = std::is_arithmetic_v<T> || std::is_enum_v<T>;

template <typename T>
struct IsStdVector : std::false_type
{
};
template <typename T, typename A>
struct IsStdVector<std::vector<T, A>> : std::true_type
{
};

/// Unsigned integer with the same size as T, holding T's little-endian bit pattern.
template <ArchivePrimitive T>
using ArchiveBits =
    std::conditional_t<sizeof(T) == 1, std::uint8_t,
                       std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                          std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;

template <typename U>
constexpr U ToLittleEndian(U value) noexcept
{
    if constexpr (std::endian::native == std::endian::big && sizeof(U) > 1)
    {
        return std::byteswap(value);
    }
    return value;
}
} // namespace detail

/// Binary serialization, written once for both directions. A type opts in with a free function found by ADL:
///
///     template <typename Archive>
///     void Serialize(Archive& archive, ChunkDesc& chunk)
///     {
///         archive(chunk.mass, chunk.name);
///         if (archive.Version() >= 2) archive(chunk.strength); // older data keeps the default
///     }
///
/// Format: FourCC magic, u32 version, then fields in order. Everything is little-endian; floats are stored
/// bit-exact; bool is one byte; strings and vectors are a u32 count followed by the elements.
class ArchiveWriter
{
public:
    static constexpr bool kIsReading = false;

    /// Appends the header to `out`; fields are appended as they are serialized.
    ArchiveWriter(std::vector<std::byte>& out, FourCC magic, std::uint32_t version)
        : m_out(&out)
        , m_version(version)
    {
        Process(magic);
        Process(version);
    }

    /// The version being written (the current format version).
    [[nodiscard]] std::uint32_t Version() const noexcept { return m_version; }

    template <typename... Ts>
    ArchiveWriter& operator()(Ts&... values)
    {
        (Process(values), ...);
        return *this;
    }

private:
    template <typename T>
    void Process(T& value)
    {
        if constexpr (detail::ArchivePrimitive<T>)
        {
            static_assert(!std::is_same_v<T, long double>, "long double has no portable layout");
            using Bits = detail::ArchiveBits<T>;
            Bits bits{};
            if constexpr (std::is_same_v<T, bool>)
            {
                bits = value ? 1 : 0;
            }
            else if constexpr (std::is_enum_v<T>)
            {
                bits = static_cast<Bits>(static_cast<std::underlying_type_t<T>>(value));
            }
            else
            {
                bits = std::bit_cast<Bits>(value);
            }
            bits = detail::ToLittleEndian(bits);
            const auto* bytes = reinterpret_cast<const std::byte*>(&bits);
            m_out->insert(m_out->end(), bytes, bytes + sizeof(Bits));
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            WriteCount(value.size());
            const auto* bytes = reinterpret_cast<const std::byte*>(value.data());
            m_out->insert(m_out->end(), bytes, bytes + value.size());
        }
        else if constexpr (detail::IsStdVector<T>::value)
        {
            static_assert(!std::is_same_v<typename T::value_type, bool>, "std::vector<bool> is not supported");
            WriteCount(value.size());
            for (auto& element : value)
            {
                Process(element);
            }
        }
        else
        {
            Serialize(*this, value); // user type, found by ADL
        }
    }

    void WriteCount(std::size_t count)
    {
        INTERI_ASSERT(count <= std::numeric_limits<std::uint32_t>::max(), "archive counts are 32-bit");
        auto count32 = static_cast<std::uint32_t>(count);
        Process(count32);
    }

    std::vector<std::byte>* m_out;
    std::uint32_t m_version;
};

/// Reads data produced by ArchiveWriter. Errors are sticky: after the first failure (truncated data, implausible
/// length) every further read yields zero/empty values and Ok() is false. Check Finish() at the end.
class ArchiveReader
{
public:
    static constexpr bool kIsReading = true;

    /// Validates the header. Fails with Corrupt (short data, wrong magic) or VersionMismatch (data newer than
    /// `currentVersion`, i.e. written by a newer build). `data` must outlive the reader.
    [[nodiscard]] static Result<ArchiveReader> Open(std::span<const std::byte> data, FourCC magic,
                                                    std::uint32_t currentVersion);

    /// The version the data was written with; may be older than the current one.
    [[nodiscard]] std::uint32_t Version() const noexcept { return m_version; }
    [[nodiscard]] bool Ok() const noexcept { return m_ok; }
    [[nodiscard]] std::size_t Remaining() const noexcept { return m_data.size() - m_position; }

    template <typename... Ts>
    ArchiveReader& operator()(Ts&... values)
    {
        (Process(values), ...);
        return *this;
    }

    /// Success only if every read succeeded and all data was consumed.
    [[nodiscard]] Result<> Finish() const;

private:
    explicit ArchiveReader(std::span<const std::byte> data) noexcept
        : m_data(data)
    {
    }

    /// Copies `size` bytes, or zero-fills `destination` and fails if fewer remain.
    bool ReadRaw(void* destination, std::size_t size) noexcept;
    /// Reads a u32 count and checks it against the remaining bytes (every element takes at least one byte), so
    /// corrupt data cannot trigger a huge allocation.
    std::size_t ReadCount() noexcept;

    template <typename T>
    void Process(T& value)
    {
        if constexpr (detail::ArchivePrimitive<T>)
        {
            using Bits = detail::ArchiveBits<T>;
            Bits bits{};
            ReadRaw(&bits, sizeof(Bits));
            bits = detail::ToLittleEndian(bits);
            if constexpr (std::is_same_v<T, bool>)
            {
                value = bits != 0;
            }
            else if constexpr (std::is_enum_v<T>)
            {
                value = static_cast<T>(static_cast<std::underlying_type_t<T>>(bits));
            }
            else
            {
                value = std::bit_cast<T>(bits);
            }
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            const std::size_t count = ReadCount();
            value.resize(count);
            ReadRaw(value.data(), count);
        }
        else if constexpr (detail::IsStdVector<T>::value)
        {
            static_assert(!std::is_same_v<typename T::value_type, bool>, "std::vector<bool> is not supported");
            const std::size_t count = ReadCount();
            value.clear();
            value.resize(count);
            for (auto& element : value)
            {
                Process(element);
            }
        }
        else
        {
            Serialize(*this, value); // user type, found by ADL
        }
    }

    std::span<const std::byte> m_data;
    std::size_t m_position = 0;
    std::uint32_t m_version = 0;
    bool m_ok = true;
};

} // namespace interi
