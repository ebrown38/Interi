// SPDX-License-Identifier: MIT
#include <interi/asset_import/AssetDatabase.h>
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/assets/Skeleton.h>
#include <interi/assets/Texture.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/hotreload/FileWatcher.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>

#include "ImportUtil.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <set>

namespace interi::asset_import
{

namespace
{

using Json = nlohmann::json;
namespace fs = std::filesystem;

constexpr std::string_view kMetaExtension = ".meta";

// --- Settings <-> JSON ----------------------------------------------------------------------------------------------

constexpr std::array<std::string_view, 2> kUpNames = {"y", "z"};
constexpr std::array<std::string_view, 2> kHandedNames = {"right", "left"};
constexpr std::array<std::string_view, 3> kNormalNames = {"fromFile", "smooth", "flat"};
constexpr std::array<std::string_view, 2> kSpaceNames = {"linear", "srgb"};
constexpr std::array<std::string_view, 4> kCompressionNames = {"none", "bc1", "bc4", "bc5"};

template <std::size_t N>
std::string_view NameOf(const std::array<std::string_view, N>& names, std::uint8_t value)
{
    return value < N ? names[value] : names[0];
}

template <typename Enum, std::size_t N>
void ReadEnum(const Json& json, const char* key, const std::array<std::string_view, N>& names, Enum& out)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string())
    {
        return;
    }
    const auto found = std::ranges::find(names, it->get<std::string>());
    if (found != names.end())
    {
        out = static_cast<Enum>(found - names.begin());
    }
}

void ReadBool(const Json& json, const char* key, bool& out)
{
    if (const auto it = json.find(key); it != json.end() && it->is_boolean())
    {
        out = it->get<bool>();
    }
}

void ReadFloat(const Json& json, const char* key, float& out)
{
    if (const auto it = json.find(key); it != json.end() && it->is_number() && std::isfinite(it->get<double>()))
    {
        out = it->get<float>();
    }
}

Json ToJson(const ModelImportSettings& s)
{
    return {{"scale", s.scale},
            {"up", NameOf(kUpNames, static_cast<std::uint8_t>(s.up))},
            {"handedness", NameOf(kHandedNames, static_cast<std::uint8_t>(s.handedness))},
            {"normals", NameOf(kNormalNames, static_cast<std::uint8_t>(s.normals))},
            {"smoothingAngle", s.smoothingAngleDegrees},
            {"generateTangents", s.generateTangents},
            {"optimize", s.optimize},
            {"importMaterials", s.importMaterials},
            {"importSkeletons", s.importSkeletons},
            {"importAnimations", s.importAnimations}};
}

ModelImportSettings ModelSettingsFromJson(const Json& json)
{
    ModelImportSettings s;
    ReadFloat(json, "scale", s.scale);
    ReadEnum(json, "up", kUpNames, s.up);
    ReadEnum(json, "handedness", kHandedNames, s.handedness);
    ReadEnum(json, "normals", kNormalNames, s.normals);
    ReadFloat(json, "smoothingAngle", s.smoothingAngleDegrees);
    ReadBool(json, "generateTangents", s.generateTangents);
    ReadBool(json, "optimize", s.optimize);
    ReadBool(json, "importMaterials", s.importMaterials);
    ReadBool(json, "importSkeletons", s.importSkeletons);
    ReadBool(json, "importAnimations", s.importAnimations);
    return s;
}

Json ToJson(const TextureImportSettings& s)
{
    return {{"colorSpace", NameOf(kSpaceNames, static_cast<std::uint8_t>(s.colorSpace))},
            {"generateMips", s.generateMips},
            {"compression", NameOf(kCompressionNames, static_cast<std::uint8_t>(s.compression))},
            {"flipY", s.flipY}};
}

TextureImportSettings TextureSettingsFromJson(const Json& json)
{
    TextureImportSettings s;
    ReadEnum(json, "colorSpace", kSpaceNames, s.colorSpace);
    ReadBool(json, "generateMips", s.generateMips);
    ReadEnum(json, "compression", kCompressionNames, s.compression);
    ReadBool(json, "flipY", s.flipY);
    return s;
}

std::string HexOf(std::uint64_t value)
{
    return std::format("{:016x}", value);
}

std::optional<std::uint64_t> ParseHex(const Json& json)
{
    if (!json.is_string() || json.get<std::string>().size() != 16)
    {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : json.get<std::string>())
    {
        const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1);
        if (digit < 0)
        {
            return std::nullopt;
        }
        value = (value << 4) | static_cast<std::uint64_t>(digit);
    }
    return value;
}

Json MetaToJson(const AssetRecord& record)
{
    Json cook = Json::object();
    Json products = Json::array();
    for (const CookedProduct& p : record.products)
    {
        products.push_back({{"name", p.name},
                            {"kind", p.kind},
                            {"path", p.path},
                            {"guid", assets::ToString(p.guid)},
                            {"hash", HexOf(p.hash)}});
    }
    cook = {{"key", HexOf(record.cookKey)},
            {"products", products},
            {"reads", record.reads},
            {"references", record.references}};
    Json meta = {
        {"version", 1}, {"guid", assets::ToString(record.guid)}, {"importer", ToString(record.kind)}, {"cook", cook}};
    if (record.kind == SourceKind::Model)
    {
        meta["settings"] = ToJson(record.model);
    }
    else if (record.kind == SourceKind::Texture)
    {
        meta["settings"] = ToJson(record.texture);
    }
    return meta;
}

// A product path must be a normalised virtual path inside the imported folder: Cook and Prune delete it.
bool IsContainedProduct(std::string_view path, std::string_view importedFolder)
{
    const auto normalized = NormalizeVirtualPath(path);
    return normalized && *normalized == path && path.size() > importedFolder.size() + 1 &&
           path.starts_with(importedFolder) && path[importedFolder.size()] == '/';
}

// Deletes a product only when it is a regular file (not a symlink) that really lies inside `<root>/<imported>`.
bool RemoveProduct(const fs::path& root, std::string_view importedFolder, std::string_view product,
                   std::error_code& error)
{
    if (!IsContainedProduct(product, importedFolder))
    {
        INTERI_LOG_WARN("assets", "refusing to delete '{}', it is outside '{}'", product, importedFolder);
        return false;
    }
    const fs::path file = root / std::string(product);
    if (fs::is_symlink(fs::symlink_status(file, error)))
    {
        return false;
    }
    const fs::path canonicalImported = fs::weakly_canonical(root / std::string(importedFolder), error);
    const fs::path canonicalFile = fs::weakly_canonical(file, error);
    const auto relative = canonicalFile.lexically_relative(canonicalImported);
    if (error || relative.empty() || *relative.begin() == "..")
    {
        return false;
    }
    return fs::remove(file, error);
}

/// Fills `record` (whose path and kind are known) from a .meta; false if the text is unusable. Products outside the
/// imported folder are dropped with a warning.
bool ReadMeta(std::string_view text, AssetRecord& record, std::string_view importedFolder)
{
    const Json meta = Json::parse(text, nullptr, false);
    if (meta.is_discarded() || !meta.is_object() || !meta.contains("guid") || !meta["guid"].is_string())
    {
        return false;
    }
    const auto guid = assets::ParseGuid(meta["guid"].get<std::string>());
    if (!guid || guid->IsNull())
    {
        return false;
    }
    record.guid = *guid;
    if (const auto settings = meta.find("settings"); settings != meta.end() && settings->is_object())
    {
        if (record.kind == SourceKind::Model)
        {
            record.model = ModelSettingsFromJson(*settings);
            if (!ValidateSettings(record.model))
            {
                record.model = {};
            }
        }
        else if (record.kind == SourceKind::Texture)
        {
            record.texture = TextureSettingsFromJson(*settings);
        }
    }
    const auto cook = meta.find("cook");
    if (cook == meta.end() || !cook->is_object())
    {
        return true;
    }
    const auto key = cook->contains("key") ? ParseHex((*cook)["key"]) : std::nullopt;
    record.cookKey = key.value_or(0);
    const auto strings = [&](const char* name, std::vector<std::string>& out)
    {
        if (const auto it = cook->find(name); it != cook->end() && it->is_array())
        {
            for (const Json& item : *it)
            {
                if (item.is_string())
                {
                    out.push_back(item.get<std::string>());
                }
            }
        }
    };
    strings("reads", record.reads);
    strings("references", record.references);
    if (const auto it = cook->find("products"); it != cook->end() && it->is_array())
    {
        for (const Json& item : *it)
        {
            CookedProduct product;
            const auto field = [&](const char* name, std::string& out)
            {
                if (item.is_object() && item.contains(name) && item[name].is_string())
                {
                    out = item[name].get<std::string>();
                }
            };
            std::string guidText;
            field("name", product.name);
            field("kind", product.kind);
            field("path", product.path);
            field("guid", guidText);
            product.guid = assets::ParseGuid(guidText).value_or(assets::Guid{});
            product.hash = item.is_object() && item.contains("hash") ? ParseHex(item["hash"]).value_or(0) : 0;
            if (!IsContainedProduct(product.path, importedFolder))
            {
                INTERI_LOG_WARN("assets", "ignoring product '{}' of '{}', it is outside '{}'", product.path,
                                record.path, importedFolder);
                continue;
            }
            record.products.push_back(std::move(product));
        }
    }
    return true;
}

std::optional<SourceKind> KindOf(std::string_view path)
{
    if (path.ends_with(".material.json"))
    {
        return SourceKind::Material;
    }
    if (IsModelPath(path))
    {
        return SourceKind::Model;
    }
    if (IsImagePath(path))
    {
        return SourceKind::Texture;
    }
    return std::nullopt;
}

std::string SanitizeName(std::string_view name, std::string_view fallback)
{
    std::string out;
    for (const char c : name)
    {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.' ? c : '_');
    }
    while (!out.empty() && out.front() == '.')
    {
        out.erase(out.begin());
    }
    return out.empty() ? std::string(fallback) : out;
}

AssetRecord MakeRecord(const std::string& path)
{
    AssetRecord record;
    record.path = path;
    record.kind = KindOf(path).value_or(SourceKind::Model);
    return record;
}

/// Reads through to `inner` and remembers what was read.
class RecordingFileSystem final : public IFileSystem
{
public:
    explicit RecordingFileSystem(const IFileSystem& inner)
        : m_inner(inner)
    {
    }

    [[nodiscard]] bool Exists(std::string_view path) const override { return m_inner.Exists(path); }

    [[nodiscard]] Result<std::vector<std::byte>> ReadFile(std::string_view path) const override
    {
        if (auto normalized = NormalizeVirtualPath(path))
        {
            m_reads.insert(*normalized);
        }
        return m_inner.ReadFile(path);
    }

    Result<void> WriteFile(std::string_view, std::span<const std::byte>) override
    {
        return MakeError(ErrorCode::Unsupported, "importers don't write");
    }

    [[nodiscard]] const std::set<std::string>& Reads() const noexcept { return m_reads; }

private:
    const IFileSystem& m_inner;
    mutable std::set<std::string> m_reads;
};

struct Output
{
    CookedProduct product;
    std::vector<std::byte> bytes;
};

} // namespace

std::string_view ToString(SourceKind kind) noexcept
{
    switch (kind)
    {
    case SourceKind::Model:
        return "model";
    case SourceKind::Texture:
        return "texture";
    case SourceKind::Material:
        return "material";
    }
    return "?";
}

assets::Guid DeriveGuid(const assets::Guid& parent, std::string_view kind, std::string_view name)
{
    const std::string text = std::format("{}:{}", kind, name);
    assets::Guid guid{.high = Hash64(text, parent.high), .low = Hash64(text, parent.low ^ 0x9E3779B97F4A7C15ULL)};
    if (guid.IsNull())
    {
        guid.low = 1;
    }
    return guid;
}

// --- Database -------------------------------------------------------------------------------------------------------

struct AssetDatabase::Impl
{
    explicit Impl(Options o)
        : options(std::move(o))
        , files(options.root)
    {
        // Product paths are compared with this folder in normalised form ("imported/", "./imported" and so on).
        if (auto folder = NormalizeVirtualPath(options.importedFolder))
        {
            options.importedFolder = std::move(*folder);
        }
        if (!options.guids)
        {
            options.guids = &assets::NewGuid;
        }
    }

    Options options;
    NativeFileSystem files;
    std::vector<AssetRecord> records; ///< Sorted by path.
    std::vector<std::string> scanWarnings;

    [[nodiscard]] AssetRecord* FindPath(std::string_view path)
    {
        const auto it = std::ranges::lower_bound(records, path, {}, &AssetRecord::path);
        return it != records.end() && it->path == path ? &*it : nullptr;
    }
    [[nodiscard]] const AssetRecord* FindPath(std::string_view path) const
    {
        const auto it = std::ranges::lower_bound(records, path, {}, &AssetRecord::path);
        return it != records.end() && it->path == path ? &*it : nullptr;
    }

    [[nodiscard]] std::string OutputRoot(const AssetRecord& record) const
    {
        return options.importedFolder + "/" + record.path;
    }

    Result<void> WriteMeta(const AssetRecord& record)
    {
        const std::string text = MetaToJson(record).dump(2) + "\n";
        return files.WriteFile(record.path + std::string(kMetaExtension), Bytes(text));
    }

    static std::span<const std::byte> Bytes(const std::string& text)
    {
        return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
    }

    [[nodiscard]] std::uint64_t ComputeKey(const AssetRecord& record, std::span<const std::byte> source,
                                           const std::vector<std::string>& reads,
                                           const std::vector<std::string>& references) const
    {
        std::uint64_t h = HashCombine(kImporterVersion, Hash64(ToString(record.kind)));
        h = HashCombine(h, (std::uint64_t{scene::kMeshVersion} << 32) | assets::kTextureVersion);
        h = HashCombine(h, (std::uint64_t{assets::kMaterialVersion} << 32) | assets::kSkeletalVersion);
        if (record.kind == SourceKind::Model)
        {
            h = HashCombine(h, Hash64(ToJson(record.model).dump()));
        }
        else if (record.kind == SourceKind::Texture)
        {
            h = HashCombine(h, Hash64(ToJson(record.texture).dump()));
        }
        h = HashCombine(h, HashBytes(source.data(), source.size()));
        for (const std::string& read : reads)
        {
            h = HashCombine(h, Hash64(read));
            const auto bytes = files.ReadFile(read);
            h = HashCombine(h, bytes ? HashBytes(bytes->data(), bytes->size()) : 0);
        }
        for (const std::string& reference : references)
        {
            h = HashCombine(h, Hash64(reference));
            const AssetRecord* target = FindPath(reference);
            h = HashCombine(h, target != nullptr ? HashCombine(target->guid.high, target->guid.low) : 0);
        }
        return h;
    }

    [[nodiscard]] bool ProductsIntact(const AssetRecord& record) const
    {
        if (record.products.empty())
        {
            return false;
        }
        return std::ranges::all_of(record.products,
                                   [&](const CookedProduct& product)
                                   {
                                       const auto bytes = files.ReadFile(product.path);
                                       return bytes && HashBytes(bytes->data(), bytes->size()) == product.hash;
                                   });
    }

    /// The cooked path of a texture source, as materials point to it.
    [[nodiscard]] std::string CookedTexturePath(const AssetRecord& texture) const
    {
        return OutputRoot(texture) + ".itex";
    }

    /// Points `ref` at the cooked texture of the source it names, if the database has it.
    void ResolveTexture(assets::TextureRef& ref, std::set<std::string>& references, std::vector<std::string>& warnings,
                        std::string_view owner) const
    {
        if (!ref.IsSet())
        {
            return;
        }
        references.insert(ref.path);
        const AssetRecord* target = FindPath(ref.path);
        if (target == nullptr || target->kind != SourceKind::Texture)
        {
            warnings.push_back(std::format("{}: texture '{}' is not in the project", owner, ref.path));
            return;
        }
        ref.path = CookedTexturePath(*target);
        ref.guid = target->guid;
    }

    Result<void> CookTexture(AssetRecord& record, std::span<const std::byte> source, std::vector<Output>& outputs) const
    {
        INTERI_TRY_ASSIGN(texture, ImportTexture(source, record.texture));
        outputs.push_back({.product = {.name = "texture",
                                       .kind = "texture",
                                       .path = CookedTexturePath(record),
                                       .guid = record.guid,
                                       .hash = 0},
                           .bytes = assets::WriteTextureAsset(texture)});
        return {};
    }

    Result<void> CookMaterial(AssetRecord& record, std::span<const std::byte> source, std::set<std::string>& references,
                              std::vector<std::string>& warnings, std::vector<Output>& outputs) const
    {
        INTERI_TRY_ASSIGN(parsed, assets::ParseMaterial(
                                      std::string_view(reinterpret_cast<const char*>(source.data()), source.size())));
        for (const std::string& warning : parsed.warnings)
        {
            warnings.push_back(std::format("{}: {}", record.path, warning));
        }
        assets::PbrMaterial& m = parsed.material;
        for (assets::TextureRef* ref : {&m.baseColorTexture, &m.metallicRoughnessTexture, &m.normalTexture,
                                        &m.occlusionTexture, &m.emissiveTexture})
        {
            ResolveTexture(*ref, references, warnings, record.path);
        }
        outputs.push_back({.product = {.name = "material",
                                       .kind = "material",
                                       .path = OutputRoot(record) + ".imat",
                                       .guid = record.guid,
                                       .hash = 0},
                           .bytes = assets::WriteMaterialAsset(m)});
        return {};
    }

    Result<void> CookModel(AssetRecord& record, const RecordingFileSystem& reader, std::set<std::string>& references,
                           std::vector<std::string>& warnings, std::vector<Output>& outputs) const
    {
        INTERI_TRY_ASSIGN(model, ImportModel(reader, record.path, record.model));
        for (const std::string& warning : model.warnings)
        {
            warnings.push_back(std::format("{}: {}", record.path, warning));
        }
        const std::string root = OutputRoot(record);
        std::map<std::string, int> used;
        const auto unique = [&](std::string_view kind, std::string_view name, std::string_view fallback)
        {
            const std::string base = SanitizeName(name, fallback);
            const int count = used[std::string(kind) + ":" + base]++;
            return count == 0 ? base : std::format("{}_{}", base, count + 1);
        };
        const auto add = [&](std::string_view kind, const std::string& name, std::string_view extension,
                             std::vector<std::byte> bytes)
        {
            CookedProduct product{.name = name,
                                  .kind = std::string(kind),
                                  .path = std::format("{}/{}.{}", root, name, extension),
                                  .guid = DeriveGuid(record.guid, kind, name),
                                  .hash = 0};
            outputs.push_back({.product = product, .bytes = std::move(bytes)});
            return outputs.back().product;
        };
        ModelManifest manifest;
        manifest.source = record.path;

        // Embedded images: colour data (base colour, emissive) is sRGB, everything else linear.
        std::vector<assets::ColorSpace> spaces(model.images.size(), assets::ColorSpace::Linear);
        std::vector<bool> used_by_colour(model.images.size(), false);
        const auto imageIndex = [&](const assets::TextureRef& ref) -> std::optional<std::size_t>
        {
            if (!ref.path.starts_with("embedded:"))
            {
                return std::nullopt;
            }
            std::size_t index = 0;
            const std::string_view digits = std::string_view(ref.path).substr(9);
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), index);
            if (error != std::errc() || end != digits.data() + digits.size() || index >= model.images.size())
            {
                return std::nullopt;
            }
            return index;
        };
        for (const assets::PbrMaterial& m : model.materials)
        {
            for (const assets::TextureRef* ref : {&m.baseColorTexture, &m.emissiveTexture})
            {
                if (const auto index = imageIndex(*ref))
                {
                    spaces[*index] = assets::ColorSpace::Srgb;
                    used_by_colour[*index] = true;
                }
            }
        }
        std::vector<CookedProduct> imageProducts(model.images.size());
        for (std::size_t i = 0; i < model.images.size(); ++i)
        {
            auto texture = ImportTexture(model.images[i].bytes,
                                         {.colorSpace = used_by_colour[i] ? assets::ColorSpace::Srgb : spaces[i]});
            if (!texture)
            {
                warnings.push_back(std::format("{}: embedded image '{}': {}", record.path, model.images[i].name,
                                               texture.error().message));
                continue;
            }
            imageProducts[i] = add("texture", unique("texture", model.images[i].name, "image"), "itex",
                                   assets::WriteTextureAsset(*texture));
            manifest.textures.push_back(imageProducts[i].path);
        }

        std::vector<std::string> materialPaths(model.materials.size());
        for (std::size_t i = 0; i < model.materials.size(); ++i)
        {
            assets::PbrMaterial material = model.materials[i];
            for (assets::TextureRef* ref :
                 {&material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture,
                  &material.occlusionTexture, &material.emissiveTexture})
            {
                if (const auto index = imageIndex(*ref))
                {
                    if (!imageProducts[*index].path.empty())
                    {
                        ref->path = imageProducts[*index].path;
                        ref->guid = imageProducts[*index].guid;
                    }
                    else
                    {
                        ref->path.clear();
                    }
                }
                else
                {
                    ResolveTexture(*ref, references, warnings, record.path);
                }
            }
            materialPaths[i] = add("material", unique("material", material.name, "material"), "imat",
                                   assets::WriteMaterialAsset(material))
                                   .path;
            manifest.materials.push_back(materialPaths[i]);
        }

        std::vector<std::string> skeletonPaths(model.skeletons.size());
        for (std::size_t i = 0; i < model.skeletons.size(); ++i)
        {
            skeletonPaths[i] = add("skeleton", unique("skeleton", model.skeletons[i].name, "skeleton"), "iskel",
                                   assets::WriteSkeletonAsset(model.skeletons[i]))
                                   .path;
            manifest.skeletons.push_back(skeletonPaths[i]);
        }
        for (const ImportedClip& clip : model.clips)
        {
            manifest.clips.push_back(
                add("clip", unique("clip", clip.clip.name, "clip"), "ianim", assets::WriteClipAsset(clip.clip)).path);
        }
        for (const ImportedMesh& mesh : model.meshes)
        {
            ModelManifestMesh entry;
            entry.name = mesh.name;
            entry.mesh = add("mesh", unique("mesh", mesh.name, "mesh"), "imesh", scene::WriteMeshAsset(mesh.mesh)).path;
            if (mesh.skeleton >= 0 && static_cast<std::size_t>(mesh.skeleton) < skeletonPaths.size())
            {
                assets::SkinBinding skin{.skeleton = skeletonPaths[static_cast<std::size_t>(mesh.skeleton)],
                                         .influences = mesh.skin};
                entry.skin = add("skin", unique("skin", mesh.name, "skin"), "iskin", assets::WriteSkinAsset(skin)).path;
            }
            for (const std::int32_t material : mesh.slotMaterials)
            {
                entry.materials.push_back(material >= 0 && static_cast<std::size_t>(material) < materialPaths.size()
                                              ? materialPaths[static_cast<std::size_t>(material)]
                                              : std::string());
            }
            manifest.meshes.push_back(std::move(entry));
        }
        const std::string text = WriteModelManifest(manifest);
        add("manifest", "manifest", "imodel.json", std::vector<std::byte>(Bytes(text).begin(), Bytes(text).end()));
        return {};
    }

    Result<bool> CookRecord(AssetRecord& record, CookReport& report)
    {
        INTERI_TRY_ASSIGN(source, files.ReadFile(record.path));
        const std::uint64_t before = ComputeKey(record, source, record.reads, record.references);
        if (record.cookKey != 0 && record.cookKey == before && ProductsIntact(record))
        {
            return false;
        }
        RecordingFileSystem reader(files);
        std::set<std::string> references;
        std::vector<Output> outputs;
        std::vector<std::string> warnings;
        Result<void> result;
        switch (record.kind)
        {
        case SourceKind::Texture:
            result = CookTexture(record, source, outputs);
            break;
        case SourceKind::Material:
            result = CookMaterial(record, source, references, warnings, outputs);
            break;
        case SourceKind::Model:
            result = CookModel(record, reader, references, warnings, outputs);
            break;
        }
        if (!result)
        {
            return std::unexpected(result.error());
        }
        // Write everything, then forget what an earlier cook produced and this one didn't.
        std::set<std::string> kept;
        for (Output& output : outputs)
        {
            INTERI_TRY(files.WriteFile(output.product.path, output.bytes));
            output.product.hash = HashBytes(output.bytes.data(), output.bytes.size());
            kept.insert(output.product.path);
        }
        for (const CookedProduct& old : record.products)
        {
            if (!kept.contains(old.path))
            {
                std::error_code ignored;
                RemoveProduct(options.root, options.importedFolder, old.path, ignored);
            }
        }
        record.products.clear();
        for (const Output& output : outputs)
        {
            record.products.push_back(output.product);
        }
        record.reads.clear();
        for (const std::string& read : reader.Reads())
        {
            if (read != record.path)
            {
                record.reads.push_back(read);
            }
        }
        record.references.assign(references.begin(), references.end());
        record.cookKey = ComputeKey(record, source, record.reads, record.references);
        INTERI_TRY(WriteMeta(record));
        for (std::string& warning : warnings)
        {
            report.warnings.push_back(std::move(warning));
        }
        return true;
    }
};

AssetDatabase::AssetDatabase(Options options)
    : m_impl(std::make_unique<Impl>(std::move(options)))
{
}

AssetDatabase::~AssetDatabase() = default;

const std::filesystem::path& AssetDatabase::Root() const noexcept
{
    return m_impl->options.root;
}

const std::vector<AssetRecord>& AssetDatabase::Records() const noexcept
{
    return m_impl->records;
}

const std::vector<std::string>& AssetDatabase::ScanWarnings() const noexcept
{
    return m_impl->scanWarnings;
}

const AssetRecord* AssetDatabase::Find(const assets::Guid& guid) const noexcept
{
    const auto it = std::ranges::find(m_impl->records, guid, &AssetRecord::guid);
    return it != m_impl->records.end() ? &*it : nullptr;
}

const AssetRecord* AssetDatabase::FindByPath(std::string_view path) const noexcept
{
    return m_impl->FindPath(path);
}

const AssetRecord* AssetDatabase::FindByProduct(std::string_view path) const noexcept
{
    for (const AssetRecord& record : m_impl->records)
    {
        if (std::ranges::find(record.products, path, &CookedProduct::path) != record.products.end())
        {
            return &record;
        }
    }
    return nullptr;
}

Result<void> AssetDatabase::Scan()
{
    Impl& impl = *m_impl;
    impl.scanWarnings.clear();
    std::vector<std::string> sources;
    std::error_code error;
    const fs::path imported = impl.options.root / impl.options.importedFolder;
    for (auto it = fs::recursive_directory_iterator(impl.options.root, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_directory(error) && it->path() == imported)
        {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error))
        {
            continue;
        }
        std::string path = fs::relative(it->path(), impl.options.root, error).generic_string();
        if (!path.ends_with(kMetaExtension) && KindOf(path))
        {
            sources.push_back(std::move(path));
        }
    }
    if (error)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("scanning '{}': {}", impl.options.root.string(), error.message()));
    }
    std::ranges::sort(sources);
    std::vector<AssetRecord> records;
    std::set<assets::Guid> seen;
    for (const std::string& path : sources)
    {
        AssetRecord record = MakeRecord(path);
        bool fresh = true;
        if (const auto text = impl.files.ReadFile(path + std::string(kMetaExtension)))
        {
            if (ReadMeta(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), record,
                         impl.options.importedFolder))
            {
                fresh = false;
            }
            else
            {
                impl.scanWarnings.push_back(std::format("{}: unreadable .meta replaced by a default one", path));
                record = MakeRecord(path);
            }
        }
        if (!fresh && !seen.insert(record.guid).second)
        {
            impl.scanWarnings.push_back(
                std::format("{}: guid already used by another asset; generated a new one", path));
            record = MakeRecord(path);
            fresh = true;
        }
        if (fresh)
        {
            do
            {
                record.guid = impl.options.guids();
            } while (record.guid.IsNull() || seen.contains(record.guid));
            seen.insert(record.guid);
            INTERI_TRY(impl.WriteMeta(record));
        }
        records.push_back(std::move(record));
    }
    impl.records = std::move(records);
    return {};
}

Result<void> AssetDatabase::SetModelSettings(std::string_view path, const ModelImportSettings& settings)
{
    AssetRecord* record = m_impl->FindPath(path);
    if (record == nullptr || record->kind != SourceKind::Model)
    {
        return MakeError(ErrorCode::NotFound, std::format("'{}' is not a model in the database", path));
    }
    INTERI_TRY(ValidateSettings(settings));
    record->model = settings;
    return m_impl->WriteMeta(*record);
}

Result<void> AssetDatabase::SetTextureSettings(std::string_view path, const TextureImportSettings& settings)
{
    AssetRecord* record = m_impl->FindPath(path);
    if (record == nullptr || record->kind != SourceKind::Texture)
    {
        return MakeError(ErrorCode::NotFound, std::format("'{}' is not a texture in the database", path));
    }
    record->texture = settings;
    return m_impl->WriteMeta(*record);
}

Result<bool> AssetDatabase::Cook(std::string_view path)
{
    AssetRecord* record = m_impl->FindPath(path);
    if (record == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("'{}' is not in the database", path));
    }
    CookReport report;
    return m_impl->CookRecord(*record, report);
}

CookReport AssetDatabase::CookAll()
{
    CookReport report;
    for (AssetRecord& record : m_impl->records)
    {
        auto result = m_impl->CookRecord(record, report);
        if (!result)
        {
            report.failed.push_back(std::format("{}: {}", record.path, result.error().message));
        }
        else if (*result)
        {
            report.cooked.push_back(record.path);
        }
        else
        {
            report.skipped.push_back(record.path);
        }
    }
    return report;
}

std::vector<std::string> AssetDatabase::Dependencies(std::string_view path) const
{
    const AssetRecord* record = m_impl->FindPath(path);
    std::set<std::string> result;
    if (record != nullptr)
    {
        result.insert(record->reads.begin(), record->reads.end());
        result.insert(record->references.begin(), record->references.end());
    }
    return {result.begin(), result.end()};
}

std::vector<std::string> AssetDatabase::Dependents(std::string_view path, bool transitive) const
{
    std::set<std::string> result;
    std::vector<std::string> frontier = {std::string(path)};
    while (!frontier.empty())
    {
        const std::string current = std::move(frontier.back());
        frontier.pop_back();
        for (const AssetRecord& record : m_impl->records)
        {
            const bool depends = std::ranges::find(record.reads, current) != record.reads.end() ||
                                 std::ranges::find(record.references, current) != record.references.end();
            if (depends && record.path != path && result.insert(record.path).second && transitive)
            {
                frontier.push_back(record.path);
            }
        }
    }
    return {result.begin(), result.end()};
}

std::vector<std::string> AssetDatabase::Prune()
{
    Impl& impl = *m_impl;
    std::vector<std::string> removed;
    std::vector<fs::path> metas;
    std::error_code error;
    const fs::path imported = impl.options.root / impl.options.importedFolder;
    for (auto it = fs::recursive_directory_iterator(impl.options.root, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_directory(error) && it->path() == imported)
        {
            it.disable_recursion_pending();
        }
        else if (it->is_regular_file(error) && it->path().extension() == kMetaExtension)
        {
            metas.push_back(it->path());
        }
    }
    std::ranges::sort(metas);
    for (const fs::path& meta : metas)
    {
        fs::path source = meta;
        source.replace_extension();
        if (fs::exists(source, error))
        {
            continue;
        }
        const std::string sourcePath = fs::relative(source, impl.options.root, error).generic_string();
        AssetRecord record = MakeRecord(sourcePath);
        if (const auto text = impl.files.ReadFile(sourcePath + std::string(kMetaExtension)))
        {
            (void)ReadMeta(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), record,
                           impl.options.importedFolder);
        }
        for (const CookedProduct& product : record.products)
        {
            if (RemoveProduct(impl.options.root, impl.options.importedFolder, product.path, error))
            {
                removed.push_back(product.path);
            }
        }
        // Empty folders left in the imported tree.
        for (fs::path folder = (impl.options.root / impl.options.importedFolder / sourcePath);
             folder != imported && !error; folder = folder.parent_path())
        {
            if (!fs::is_directory(folder, error) || !fs::is_empty(folder, error))
            {
                break;
            }
            fs::remove(folder, error);
        }
        if (fs::remove(meta, error))
        {
            removed.push_back(fs::relative(meta, impl.options.root, error).generic_string());
        }
    }
    return removed;
}

// --- Watcher --------------------------------------------------------------------------------------------------------

struct AssetWatcher::Impl
{
    explicit Impl(AssetDatabase& db)
        : database(db)
    {
        watcher.Watch(database.Root());
    }

    AssetDatabase& database;
    hotreload::FileWatcher watcher;
    bool primed = false;
};

AssetWatcher::AssetWatcher(AssetDatabase& database)
    : m_impl(std::make_unique<Impl>(database))
{
}

AssetWatcher::~AssetWatcher() = default;

AssetWatcher::Outcome AssetWatcher::Update()
{
    Outcome outcome;
    const bool changed = m_impl->watcher.Poll();
    if (!m_impl->primed)
    {
        m_impl->primed = true;
        return outcome;
    }
    if (!changed)
    {
        return outcome;
    }
    outcome.changed = true;
    if (auto scanned = m_impl->database.Scan(); !scanned)
    {
        outcome.report.failed.push_back(scanned.error().message);
        return outcome;
    }
    outcome.report = m_impl->database.CookAll();
    (void)m_impl->watcher.Poll(); // the cook's own writes are not changes
    return outcome;
}

} // namespace interi::asset_import
