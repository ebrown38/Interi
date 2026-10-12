// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace interi
{

/// Broad error categories. The accompanying Error::message carries the specifics.
enum class ErrorCode : std::uint8_t
{
    Unknown,
    InvalidArgument,
    OutOfMemory,
    NotFound,
    IoError,
    Corrupt,         ///< Data failed validation (bad magic, checksum, truncated stream).
    VersionMismatch, ///< Data was written by a newer, incompatible format version.
    Unsupported,
    Degenerate, ///< Geometric input has no valid result (e.g. coplanar points for a 3D hull).
};

/// Returns the enumerator name, e.g. "InvalidArgument".
[[nodiscard]] constexpr std::string_view ToString(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::OutOfMemory:
        return "OutOfMemory";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::IoError:
        return "IoError";
    case ErrorCode::Corrupt:
        return "Corrupt";
    case ErrorCode::VersionMismatch:
        return "VersionMismatch";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::Degenerate:
        return "Degenerate";
    }
    return "Unknown";
}

/// Error value carried by Result.
struct Error
{
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
};

/// Value-or-error return type for operations that can fail at runtime (I/O, parsing, invalid data).
/// Programming errors use INTERI_ASSERT instead.
template <typename T = void>
using Result = std::expected<T, Error>;

/// Builds the error side of a Result: `return MakeError(ErrorCode::NotFound, "no such asset");`
[[nodiscard]] inline std::unexpected<Error> MakeError(ErrorCode code, std::string message = {})
{
    return std::unexpected<Error>(Error{.code = code, .message = std::move(message)});
}

} // namespace interi

#define INTERI_DETAIL_CONCAT_INNER(a, b) a##b
#define INTERI_DETAIL_CONCAT(a, b) INTERI_DETAIL_CONCAT_INNER(a, b)

/// Evaluates a Result-returning expression and returns its error from the enclosing function if it failed.
#define INTERI_TRY(expression)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (auto&& interiTryResult = (expression); !interiTryResult.has_value())                                       \
        {                                                                                                              \
            return std::unexpected<::interi::Error>(std::move(interiTryResult).error());                               \
        }                                                                                                              \
    } while (false)

/// Declares `name` from a successful Result, or returns the error from the enclosing function.
#define INTERI_TRY_ASSIGN(name, expression)                                                                            \
    auto&& INTERI_DETAIL_CONCAT(interiTryTmp, __LINE__) = (expression);                                                \
    if (!INTERI_DETAIL_CONCAT(interiTryTmp, __LINE__).has_value())                                                     \
    {                                                                                                                  \
        return std::unexpected<::interi::Error>(std::move(INTERI_DETAIL_CONCAT(interiTryTmp, __LINE__)).error());      \
    }                                                                                                                  \
    auto name = *std::move(INTERI_DETAIL_CONCAT(interiTryTmp, __LINE__)) /* NOLINT(bugprone-macro-parentheses) */
