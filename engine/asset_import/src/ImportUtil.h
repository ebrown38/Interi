// SPDX-License-Identifier: MIT
#pragma once

// Small helpers the importers share. Private to the library.

#include <interi/core/Result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::asset_import
{

/// Standard base64 (padding optional, whitespace not allowed).
[[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text);

/// The folder part of a virtual path ("" for a file at the root).
[[nodiscard]] std::string DirectoryOf(std::string_view path);

/// Percent-decodes a URI reference.
[[nodiscard]] std::string DecodeUri(std::string_view uri);

/// `directory` + `/` + a (possibly percent-encoded) relative URI, normalised. InvalidArgument for '..', drive letters
/// and absolute URIs, so a model can never name a file outside the file system it is imported from.
[[nodiscard]] Result<std::string> ResolveReference(std::string_view directory, std::string_view uri);

/// Lower-case extension including the dot ("" if none).
[[nodiscard]] std::string ExtensionOf(std::string_view path);

/// The file name without folder or extension.
[[nodiscard]] std::string StemOf(std::string_view path);

} // namespace interi::asset_import
