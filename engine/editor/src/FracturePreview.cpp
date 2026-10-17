// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/destruction/Material.h>
#include <interi/editor/FracturePreview.h>

#include <format>
#include <fstream>
#include <iterator>

namespace interi::editor
{

namespace
{

Result<std::string> ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot open '{}'", path.string()));
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

std::filesystem::path SidecarPath(const std::filesystem::path& gltf)
{
    return gltf.parent_path() / (gltf.stem().string() + ".fracture.json");
}

std::uint64_t FracturePreview::SettingsKey(const destruction::CookConfig& config)
{
    return Hash64(destruction::WriteCookConfig(config));
}

Result<void> FracturePreview::Open(const std::string& key, const std::filesystem::path& gltf,
                                   const std::filesystem::path& defaultMaterials)
{
    INTERI_TRY_ASSIGN(scene, destruction::ImportGltf(gltf));
    destruction::CookConfig config = destruction::DefaultCookConfig();
    const std::filesystem::path sidecar = SidecarPath(gltf);
    if (std::filesystem::exists(sidecar))
    {
        INTERI_TRY_ASSIGN(text, ReadText(sidecar));
        INTERI_TRY_ASSIGN(parsed, destruction::ParseCookConfig(text));
        config = std::move(parsed);
    }
    if (config.materialsFile.empty())
    {
        config.materialsFile = std::filesystem::relative(defaultMaterials, gltf.parent_path()).generic_string();
    }
    INTERI_TRY_ASSIGN(materialsText, ReadText(gltf.parent_path() / config.materialsFile));
    INTERI_TRY_ASSIGN(materials, destruction::ParseMaterials(materialsText));
    Add(key, std::move(scene), std::move(config), std::move(materials.materials));
    m_entries.at(key).source = gltf;
    return {};
}

void FracturePreview::Add(const std::string& key, destruction::ImportedScene scene, destruction::CookConfig config,
                          std::vector<destruction::DestructionMaterial> materials)
{
    Entry entry;
    entry.scene = std::move(scene);
    entry.config = std::move(config);
    entry.materials = std::move(materials);
    m_entries.insert_or_assign(key, std::move(entry));
}

FracturePreview::Entry* FracturePreview::Find(std::string_view key) noexcept
{
    const auto found = m_entries.find(key);
    return found == m_entries.end() ? nullptr : &found->second;
}

std::vector<std::string> FracturePreview::Keys() const
{
    std::vector<std::string> keys;
    keys.reserve(m_entries.size());
    for (const auto& [key, entry] : m_entries)
    {
        keys.push_back(key);
    }
    return keys;
}

std::size_t FracturePreview::Update(JobSystem& jobs)
{
    std::size_t cooked = 0;
    for (auto& [key, entry] : m_entries)
    {
        const std::uint64_t settings = SettingsKey(entry.config);
        if (entry.revision != 0 && settings == entry.cookedKey)
        {
            continue;
        }
        entry.cookedKey = settings;
        ++entry.revision;
        ++cooked;
        auto asset = destruction::CookScene(entry.scene, entry.config, entry.materials, jobs);
        if (asset)
        {
            entry.asset = std::make_shared<const destruction::DestructibleAsset>(std::move(*asset));
            entry.error.clear();
        }
        else
        {
            entry.error = asset.error().message;
        }
    }
    return cooked;
}

Result<destruction::CookReport> FracturePreview::SaveAndCook(std::string_view key, JobSystem& jobs)
{
    Entry* entry = Find(key);
    if (entry == nullptr || entry->source.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("'{}' has no glTF source to cook", key));
    }
    const std::string text = destruction::WriteCookConfig(entry->config);
    {
        std::ofstream file(SidecarPath(entry->source), std::ios::binary | std::ios::trunc);
        if (!file || !file.write(text.data(), static_cast<std::streamsize>(text.size())))
        {
            return MakeError(ErrorCode::IoError, std::format("cannot write '{}'", SidecarPath(entry->source).string()));
        }
    }
    destruction::CookRequest request;
    request.input = entry->source;
    return destruction::Cook(request, jobs);
}

} // namespace interi::editor
