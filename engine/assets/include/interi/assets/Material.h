// SPDX-License-Identifier: MIT
#pragma once

// PBR material assets (metallic-roughness, the glTF model). `.material.json` is the authored form (sorted keys,
// round-trips); `.imat` is the cooked binary (magic IMAT, version, payload hash, payload archive IMAP).

#include <interi/assets/Guid.h>
#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>
#include <interi/platform/FileSystem.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::assets
{

enum class AlphaMode : std::uint8_t
{
    Opaque,
    Mask, ///< Texels below `alphaCutoff` are discarded.
    Blend,
};
inline constexpr std::uint8_t kAlphaModeCount = 3;

[[nodiscard]] std::string_view ToString(AlphaMode mode) noexcept;

/// A texture a material samples: a virtual path (project-relative) and, once the asset database knows the file, its
/// guid. Empty path = no texture.
struct TextureRef
{
    std::string path;
    Guid guid;
    std::uint32_t uvSet = 0;

    [[nodiscard]] bool IsSet() const noexcept { return !path.empty(); }
    friend bool operator==(const TextureRef&, const TextureRef&) = default;
};

struct PbrMaterial
{
    std::string name;
    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA factor, multiplied with the texture.
    float metallic = 1.0f;
    float roughness = 1.0f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    TextureRef baseColorTexture;         ///< sRGB.
    TextureRef metallicRoughnessTexture; ///< Linear: green = roughness, blue = metallic.
    TextureRef normalTexture;            ///< Linear, tangent space.
    TextureRef occlusionTexture;         ///< Linear: red.
    TextureRef emissiveTexture;          ///< sRGB.

    friend bool operator==(const PbrMaterial&, const PbrMaterial&) = default;
};

/// Factors finite, metallic, roughness, occlusion strength and cutoff within [0, 1], colour and emissive not negative.
[[nodiscard]] Result<void> ValidateMaterial(const PbrMaterial& material);

struct ParsedMaterial
{
    PbrMaterial material;
    std::vector<std::string> warnings; ///< Unknown fields.
};

/// `{"version": 1, "name", "baseColor": [r,g,b,a], "metallic", "roughness", "emissive": [r,g,b], "normalScale",
/// "occlusionStrength", "alphaMode": opaque|mask|blend, "alphaCutoff", "doubleSided", "textures": {"baseColor" |
/// "metallicRoughness" | "normal" | "occlusion" | "emissive": {"path", "guid", "uvSet"}}}`. Missing fields keep the
/// glTF defaults; unknown fields are warnings, anything malformed an error.
[[nodiscard]] Result<ParsedMaterial> ParseMaterial(std::string_view json);
/// Canonical text (sorted keys); ParseMaterial(WriteMaterial(m)) gives `m` back.
[[nodiscard]] std::string WriteMaterial(const PbrMaterial& material);

inline constexpr FourCC kMaterialMagic = MakeFourCC("IMAT");
inline constexpr std::uint32_t kMaterialVersion = 1;

[[nodiscard]] std::vector<std::byte> WriteMaterialAsset(const PbrMaterial& material);
[[nodiscard]] Result<PbrMaterial> ReadMaterialAsset(std::span<const std::byte> data);
[[nodiscard]] Result<PbrMaterial> LoadMaterialAsset(const IFileSystem& files, std::string_view path);

} // namespace interi::assets
