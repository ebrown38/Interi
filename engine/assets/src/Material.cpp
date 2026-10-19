// SPDX-License-Identifier: MIT
#include <interi/assets/Material.h>

#include "AssetIo.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <set>
#include <utility>

namespace interi::assets
{

namespace
{

using Json = nlohmann::json;

constexpr FourCC kPayloadMagic = MakeFourCC("IMAP");
constexpr std::array<std::string_view, kAlphaModeCount> kAlphaNames = {"opaque", "mask", "blend"};

struct TextureSlot
{
    std::string_view key;
    TextureRef PbrMaterial::* member;
};
constexpr std::array<TextureSlot, 5> kSlots = {{{"baseColor", &PbrMaterial::baseColorTexture},
                                                {"metallicRoughness", &PbrMaterial::metallicRoughnessTexture},
                                                {"normal", &PbrMaterial::normalTexture},
                                                {"occlusion", &PbrMaterial::occlusionTexture},
                                                {"emissive", &PbrMaterial::emissiveTexture}}};

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsNumber(const Json& value)
{
    return value.is_number() && std::isfinite(value.get<double>());
}

Result<void> ReadFloat(const Json& object, const char* key, float& out)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return {};
    }
    if (!IsNumber(*it))
    {
        return Invalid(std::format("material: \"{}\" must be a number", key));
    }
    out = it->get<float>();
    return {};
}

Result<void> ReadFloats(const Json& object, const char* key, std::span<float> out)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return {};
    }
    if (!it->is_array() || it->size() != out.size() || !std::ranges::all_of(*it, IsNumber))
    {
        return Invalid(std::format("material: \"{}\" must be an array of {} numbers", key, out.size()));
    }
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        out[i] = (*it)[i].get<float>();
    }
    return {};
}

Json TextureToJson(const TextureRef& texture)
{
    Json json = {{"path", texture.path}, {"uvSet", texture.uvSet}};
    if (!texture.guid.IsNull())
    {
        json["guid"] = ToString(texture.guid);
    }
    return json;
}

} // namespace

std::string_view ToString(AlphaMode mode) noexcept
{
    const auto index = static_cast<std::size_t>(mode);
    return index < kAlphaNames.size() ? kAlphaNames[index] : "?";
}

Result<void> ValidateMaterial(const PbrMaterial& material)
{
    const auto bad = [](std::string_view what) { return Invalid(std::format("material: {}", what)); };
    const auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
    const auto nonNegative = [](float v) { return std::isfinite(v) && v >= 0.0f; };
    if (!unit(material.metallic) || !unit(material.roughness) || !unit(material.occlusionStrength) ||
        !unit(material.alphaCutoff))
    {
        return bad("metallic, roughness, occlusionStrength and alphaCutoff must be in [0, 1]");
    }
    if (!nonNegative(material.baseColor.x) || !nonNegative(material.baseColor.y) ||
        !nonNegative(material.baseColor.z) || !unit(material.baseColor.w))
    {
        return bad("baseColor must be non-negative with alpha in [0, 1]");
    }
    if (!nonNegative(material.emissive.x) || !nonNegative(material.emissive.y) || !nonNegative(material.emissive.z))
    {
        return bad("emissive must be non-negative");
    }
    if (!std::isfinite(material.normalScale))
    {
        return bad("normalScale must be finite");
    }
    if (static_cast<std::uint8_t>(material.alphaMode) >= kAlphaModeCount)
    {
        return bad("unknown alphaMode");
    }
    return {};
}

Result<ParsedMaterial> ParseMaterial(std::string_view text)
{
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("material: not a JSON object");
    }
    ParsedMaterial parsed;
    PbrMaterial& m = parsed.material;
    static const std::set<std::string, std::less<>> known = {
        "version",           "name",      "baseColor",   "metallic",    "roughness", "emissive", "normalScale",
        "occlusionStrength", "alphaMode", "alphaCutoff", "doubleSided", "textures"};
    for (const auto& [key, value] : root.items())
    {
        if (!known.contains(key))
        {
            parsed.warnings.push_back(std::format("material: unknown field \"{}\"", key));
        }
    }
    if (const auto it = root.find("version"); it != root.end() && (!it->is_number_integer() || it->get<int>() != 1))
    {
        return Invalid("material: unsupported version");
    }
    if (const auto it = root.find("name"); it != root.end())
    {
        if (!it->is_string())
        {
            return Invalid("material: \"name\" must be a string");
        }
        m.name = it->get<std::string>();
    }
    std::array<float, 4> color = {m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w};
    INTERI_TRY(ReadFloats(root, "baseColor", color));
    m.baseColor = Vec4(color[0], color[1], color[2], color[3]);
    std::array<float, 3> emissive = {m.emissive.x, m.emissive.y, m.emissive.z};
    INTERI_TRY(ReadFloats(root, "emissive", emissive));
    m.emissive = Vec3(emissive[0], emissive[1], emissive[2]);
    INTERI_TRY(ReadFloat(root, "metallic", m.metallic));
    INTERI_TRY(ReadFloat(root, "roughness", m.roughness));
    INTERI_TRY(ReadFloat(root, "normalScale", m.normalScale));
    INTERI_TRY(ReadFloat(root, "occlusionStrength", m.occlusionStrength));
    INTERI_TRY(ReadFloat(root, "alphaCutoff", m.alphaCutoff));
    if (const auto it = root.find("alphaMode"); it != root.end())
    {
        const auto name = it->is_string() ? it->get<std::string>() : std::string();
        const auto found = std::ranges::find(kAlphaNames, name);
        if (found == kAlphaNames.end())
        {
            return Invalid("material: \"alphaMode\" must be opaque, mask or blend");
        }
        m.alphaMode = static_cast<AlphaMode>(found - kAlphaNames.begin());
    }
    if (const auto it = root.find("doubleSided"); it != root.end())
    {
        if (!it->is_boolean())
        {
            return Invalid("material: \"doubleSided\" must be a boolean");
        }
        m.doubleSided = it->get<bool>();
    }
    if (const auto it = root.find("textures"); it != root.end())
    {
        if (!it->is_object())
        {
            return Invalid("material: \"textures\" must be an object");
        }
        for (const auto& [key, value] : it->items())
        {
            const auto slot = std::ranges::find(kSlots, key, &TextureSlot::key);
            if (slot == kSlots.end())
            {
                parsed.warnings.push_back(std::format("material: unknown texture slot \"{}\"", key));
                continue;
            }
            if (!value.is_object() || !value.contains("path") || !value["path"].is_string())
            {
                return Invalid(std::format(R"(material: texture "{}" needs a "path" string)", key));
            }
            TextureRef& ref = m.*(slot->member);
            ref.path = value["path"].get<std::string>();
            if (const auto uv = value.find("uvSet"); uv != value.end())
            {
                if (!uv->is_number_unsigned())
                {
                    return Invalid(std::format(R"(material: texture "{}": "uvSet" must be an unsigned integer)", key));
                }
                ref.uvSet = uv->get<std::uint32_t>();
            }
            if (const auto guid = value.find("guid"); guid != value.end())
            {
                const auto parsedGuid = guid->is_string() ? ParseGuid(guid->get<std::string>()) : std::nullopt;
                if (!parsedGuid)
                {
                    return Invalid(std::format(R"(material: texture "{}": bad "guid")", key));
                }
                ref.guid = *parsedGuid;
            }
        }
    }
    INTERI_TRY(ValidateMaterial(m));
    return parsed;
}

std::string WriteMaterial(const PbrMaterial& m)
{
    Json json = {{"version", 1},
                 {"name", m.name},
                 {"baseColor", {m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w}},
                 {"metallic", m.metallic},
                 {"roughness", m.roughness},
                 {"emissive", {m.emissive.x, m.emissive.y, m.emissive.z}},
                 {"normalScale", m.normalScale},
                 {"occlusionStrength", m.occlusionStrength},
                 {"alphaMode", ToString(m.alphaMode)},
                 {"alphaCutoff", m.alphaCutoff},
                 {"doubleSided", m.doubleSided}};
    Json textures = Json::object();
    for (const TextureSlot& slot : kSlots)
    {
        if ((m.*(slot.member)).IsSet())
        {
            textures[std::string(slot.key)] = TextureToJson(m.*(slot.member));
        }
    }
    json["textures"] = std::move(textures);
    return json.dump(2) + "\n";
}

template <typename Archive>
void Serialize(Archive& archive, TextureRef& texture)
{
    archive(texture.path, texture.guid, texture.uvSet);
}

template <typename Archive>
void Serialize(Archive& archive, PbrMaterial& m)
{
    archive(m.name, m.baseColor, m.metallic, m.roughness, m.emissive, m.normalScale, m.occlusionStrength, m.alphaMode,
            m.alphaCutoff, m.doubleSided, m.baseColorTexture, m.metallicRoughnessTexture, m.normalTexture,
            m.occlusionTexture, m.emissiveTexture);
}

std::vector<std::byte> WriteMaterialAsset(const PbrMaterial& material)
{
    return detail::WriteHashed(kMaterialMagic, kPayloadMagic, kMaterialVersion, material);
}

Result<PbrMaterial> ReadMaterialAsset(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(
        material, detail::ReadHashed<PbrMaterial>(data, kMaterialMagic, kPayloadMagic, kMaterialVersion, "material"));
    if (auto valid = ValidateMaterial(material); !valid)
    {
        return MakeError(ErrorCode::Corrupt, valid.error().message);
    }
    return material;
}

Result<PbrMaterial> LoadMaterialAsset(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto material = ReadMaterialAsset(bytes);
    if (!material)
    {
        return MakeError(material.error().code, std::format("{}: {}", path, material.error().message));
    }
    return material;
}

} // namespace interi::assets
