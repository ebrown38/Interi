// SPDX-License-Identifier: MIT
#include <interi/core/Version.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/Toolchain.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace interi::sdk
{
namespace
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsLower(char c) noexcept
{
    return c >= 'a' && c <= 'z';
}
bool IsDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

/// Metadata whose absence is worth a warning, with its default.
struct MetadataField
{
    std::string_view key;
    std::string ModuleManifest::* member;
};

constexpr MetadataField kMetadataFields[] = {
    {"author", &ModuleManifest::author},
    {"license", &ModuleManifest::license},
    {"copyright", &ModuleManifest::copyright},
    {"description", &ModuleManifest::description},
};

/// Optional strings that default silently.
constexpr MetadataField kQuietFields[] = {
    {"icon", &ModuleManifest::icon},
    {"homepage", &ModuleManifest::homepage},
    {"library", &ModuleManifest::library},
    {"toolchain", &ModuleManifest::toolchain},
};

// The data sections ("values" to "dataTables") are parsed by interi::values (ParseModuleContent).
constexpr std::string_view kKnownKeys[] = {
    "id",        "name",     "version",    "kind",          "author",     "license",      "copyright", "description",
    "icon",      "homepage", "engine",     "library",       "toolchain",  "dependencies", "conflicts", "contracts",
    "overrides", "values",   "attributes", "attributeSets", "archetypes", "dataTables",
};

Result<std::string> GetString(const nlohmann::json& object, std::string_view key, std::string_view where)
{
    const auto it = object.find(key);
    if (!it->is_string())
    {
        return Invalid(std::format("{}: \"{}\" must be a string", where, key));
    }
    return it->get<std::string>();
}

Result<const nlohmann::json*> GetArray(const nlohmann::json& object, std::string_view key)
{
    const auto it = object.find(key);
    if (it == object.end())
    {
        return nullptr;
    }
    if (!it->is_array())
    {
        return Invalid(std::format("\"{}\" must be an array", key));
    }
    return &*it;
}

/// `{ "id": ..., "version": <range> }` entries of dependencies and conflicts.
Result<std::pair<std::string, VersionRange>> ParseIdAndRange(const nlohmann::json& entry, std::string_view section,
                                                             std::size_t index)
{
    const std::string where = std::format("{}[{}]", section, index);
    if (!entry.is_object())
    {
        return Invalid(std::format("{} is not an object", where));
    }
    if (!entry.contains("id"))
    {
        return Invalid(std::format("{} has no \"id\"", where));
    }
    INTERI_TRY_ASSIGN(id, GetString(entry, "id", where));
    if (!IsValidModuleId(id))
    {
        return Invalid(std::format("{}: invalid module id '{}'", where, id));
    }
    VersionRange range;
    if (entry.contains("version"))
    {
        INTERI_TRY_ASSIGN(text, GetString(entry, "version", where));
        auto parsed = ParseVersionRange(text);
        if (!parsed)
        {
            return Invalid(std::format("{}: {}", where, parsed.error().message));
        }
        range = std::move(*parsed);
    }
    return std::pair{std::move(id), std::move(range)};
}

Result<void> ParseSections(const nlohmann::json& root, ModuleManifest& manifest)
{
    INTERI_TRY_ASSIGN(dependencies, GetArray(root, "dependencies"));
    if (dependencies != nullptr)
    {
        for (std::size_t i = 0; i < dependencies->size(); ++i)
        {
            const nlohmann::json& entry = (*dependencies)[i];
            INTERI_TRY_ASSIGN(idAndRange, ParseIdAndRange(entry, "dependencies", i));
            ModuleDependency dependency{
                .id = std::move(idAndRange.first), .version = std::move(idAndRange.second), .optional = false};
            if (const auto it = entry.find("optional"); it != entry.end())
            {
                if (!it->is_boolean())
                {
                    return Invalid(std::format("dependencies[{}]: \"optional\" must be a boolean", i));
                }
                dependency.optional = it->get<bool>();
            }
            if (dependency.id == manifest.id)
            {
                return Invalid("a module can't depend on itself");
            }
            if (std::ranges::any_of(manifest.dependencies,
                                    [&](const ModuleDependency& d) { return d.id == dependency.id; }))
            {
                return Invalid(std::format("duplicate dependency '{}'", dependency.id));
            }
            manifest.dependencies.push_back(std::move(dependency));
        }
    }

    INTERI_TRY_ASSIGN(conflicts, GetArray(root, "conflicts"));
    if (conflicts != nullptr)
    {
        for (std::size_t i = 0; i < conflicts->size(); ++i)
        {
            INTERI_TRY_ASSIGN(idAndRange, ParseIdAndRange((*conflicts)[i], "conflicts", i));
            if (idAndRange.first == manifest.id)
            {
                return Invalid("a module can't conflict with itself");
            }
            manifest.conflicts.push_back({.id = std::move(idAndRange.first), .version = std::move(idAndRange.second)});
        }
    }

    INTERI_TRY_ASSIGN(contracts, GetArray(root, "contracts"));
    if (contracts != nullptr)
    {
        if (manifest.kind != ModuleKind::Extension && !contracts->empty())
        {
            return Invalid("only an Extension may declare shared contract ids");
        }
        for (std::size_t i = 0; i < contracts->size(); ++i)
        {
            const nlohmann::json& entry = (*contracts)[i];
            if (!entry.is_string() || !IsValidItemName(entry.get<std::string>()))
            {
                return Invalid(std::format("contracts[{}] is not a shared id (letters, digits and \"._-\")", i));
            }
            manifest.contracts.push_back(entry.get<std::string>());
        }
    }

    INTERI_TRY_ASSIGN(overrides, GetArray(root, "overrides"));
    if (overrides != nullptr)
    {
        for (std::size_t i = 0; i < overrides->size(); ++i)
        {
            const nlohmann::json& entry = (*overrides)[i];
            const std::string where = std::format("overrides[{}]", i);
            if (!entry.is_object() || !entry.contains("target"))
            {
                return Invalid(std::format("{} needs a \"target\"", where));
            }
            INTERI_TRY_ASSIGN(target, GetString(entry, "target", where));
            if (!IsLocalId(target))
            {
                return Invalid(std::format("{}: '{}' is not a local id (<module id>:<name>); shared ids are "
                                           "resolved by priority without an override",
                                           where, target));
            }
            if (target.substr(0, target.find(':')) == manifest.id)
            {
                return Invalid(std::format("{}: a module can't override its own item '{}'", where, target));
            }
            VersionRange range;
            if (entry.contains("version"))
            {
                INTERI_TRY_ASSIGN(text, GetString(entry, "version", where));
                auto parsed = ParseVersionRange(text);
                if (!parsed)
                {
                    return Invalid(std::format("{}: {}", where, parsed.error().message));
                }
                range = std::move(*parsed);
            }
            manifest.overrides.push_back({.target = std::move(target), .version = std::move(range)});
        }
    }
    return {};
}

} // namespace

std::string_view ToString(ModuleKind kind) noexcept
{
    switch (kind)
    {
    case ModuleKind::Module:
        return "Module";
    case ModuleKind::Extension:
        return "Extension";
    case ModuleKind::Plugin:
        return "Plugin";
    }
    return "Module";
}

bool IsValidModuleId(std::string_view id) noexcept
{
    std::size_t labels = 0;
    std::size_t begin = 0;
    while (begin <= id.size())
    {
        const std::size_t dot = id.find('.', begin);
        const std::string_view label = id.substr(begin, dot == std::string_view::npos ? id.npos : dot - begin);
        if (label.empty() || !(IsLower(label.front()) || IsDigit(label.front())))
        {
            return false;
        }
        if (!std::ranges::all_of(label, [](char c) { return IsLower(c) || IsDigit(c) || c == '_' || c == '-'; }))
        {
            return false;
        }
        ++labels;
        if (dot == std::string_view::npos)
        {
            break;
        }
        begin = dot + 1;
    }
    return labels >= 2;
}

bool IsLocalId(std::string_view id) noexcept
{
    const std::size_t colon = id.find(':');
    return colon != std::string_view::npos && IsValidModuleId(id.substr(0, colon)) &&
           IsValidItemName(id.substr(colon + 1));
}

std::string_view OwnerOf(std::string_view id) noexcept
{
    return IsLocalId(id) ? id.substr(0, id.find(':')) : std::string_view{};
}

bool IsValidItemName(std::string_view name) noexcept
{
    return !name.empty() && name.front() != '.' &&
           std::ranges::all_of(
               name, [](char c)
               { return IsLower(c) || (c >= 'A' && c <= 'Z') || IsDigit(c) || c == '.' || c == '_' || c == '-'; });
}

Result<ModuleManifest> ParseManifest(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded())
    {
        return Invalid("module.json is not valid JSON");
    }
    if (!root.is_object())
    {
        return Invalid("module.json must be an object");
    }

    ModuleManifest manifest;
    if (!root.contains("id"))
    {
        return Invalid("module.json has no \"id\"");
    }
    INTERI_TRY_ASSIGN(idText, GetString(root, "id", "module.json"));
    manifest.id = std::move(idText);
    if (!IsValidModuleId(manifest.id))
    {
        return Invalid(std::format("invalid module id '{}' (reverse-DNS, lowercase: com.author.name)", manifest.id));
    }
    const std::string where = std::format("module '{}'", manifest.id);

    if (!root.contains("version"))
    {
        return Invalid(std::format("{} has no \"version\"", where));
    }
    INTERI_TRY_ASSIGN(versionText, GetString(root, "version", where));
    auto version = ParseSemVer(versionText);
    if (!version)
    {
        return Invalid(std::format("{}: {}", where, version.error().message));
    }
    manifest.version = std::move(*version);

    if (root.contains("name"))
    {
        INTERI_TRY_ASSIGN(nameText, GetString(root, "name", where));
        manifest.name = std::move(nameText);
    }
    else
    {
        manifest.name = manifest.id;
        manifest.warnings.push_back(std::format("{}: missing \"name\", using the id", where));
    }

    if (root.contains("kind"))
    {
        INTERI_TRY_ASSIGN(kind, GetString(root, "kind", where));
        if (kind == "Module")
        {
            manifest.kind = ModuleKind::Module;
        }
        else if (kind == "Extension")
        {
            manifest.kind = ModuleKind::Extension;
        }
        else if (kind == "Plugin")
        {
            manifest.kind = ModuleKind::Plugin;
        }
        else
        {
            return Invalid(std::format("{}: unknown kind '{}' (Module, Extension or Plugin)", where, kind));
        }
    }
    else
    {
        manifest.warnings.push_back(std::format("{}: missing \"kind\", using Module", where));
    }

    for (const MetadataField& field : kMetadataFields)
    {
        if (root.contains(field.key))
        {
            INTERI_TRY_ASSIGN(metaText, GetString(root, field.key, where));
            manifest.*field.member = std::move(metaText);
        }
        else
        {
            manifest.warnings.push_back(std::format("{}: missing \"{}\"", where, field.key));
        }
    }
    for (const MetadataField& field : kQuietFields)
    {
        if (root.contains(field.key))
        {
            INTERI_TRY_ASSIGN(quietText, GetString(root, field.key, where));
            manifest.*field.member = std::move(quietText);
        }
    }
    if (!manifest.library.empty() && !IsValidItemName(manifest.library))
    {
        return Invalid(std::format("{}: \"library\" must be a plain base name, got '{}'", where, manifest.library));
    }
    if (manifest.library.find('.') != std::string::npos)
    {
        return Invalid(
            std::format("{}: \"library\" is a base name without extension, got '{}'", where, manifest.library));
    }

    if (root.contains("engine"))
    {
        INTERI_TRY_ASSIGN(engineText, GetString(root, "engine", where));
        auto engine = ParseVersionRange(engineText);
        if (!engine)
        {
            return Invalid(std::format("{}: \"engine\": {}", where, engine.error().message));
        }
        manifest.engine = std::move(*engine);
    }
    else
    {
        manifest.warnings.push_back(std::format("{}: missing \"engine\", assuming any engine version", where));
    }

    if (auto sections = ParseSections(root, manifest); !sections)
    {
        return Invalid(std::format("{}: {}", where, sections.error().message));
    }

    for (const auto& [key, value] : root.items())
    {
        if (std::ranges::find(kKnownKeys, std::string_view(key)) == std::end(kKnownKeys))
        {
            manifest.warnings.push_back(std::format("{}: unknown section \"{}\" ignored", where, key));
        }
    }
    return manifest;
}

Result<ModuleManifest> LoadManifest(const std::filesystem::path& directory)
{
    const std::filesystem::path path = directory / "module.json";
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("can't read {}", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    INTERI_TRY_ASSIGN(manifest, ParseManifest(text.str()));
    manifest.directory = directory;
    return manifest;
}

HostInfo CurrentHost()
{
    const Version engine = GetEngineVersion();
    return HostInfo{.engineVersion = SemVer{.major = static_cast<std::uint32_t>(engine.major),
                                            .minor = static_cast<std::uint32_t>(engine.minor),
                                            .patch = static_cast<std::uint32_t>(engine.patch),
                                            .prerelease = {},
                                            .build = {}},
                    .toolchain = std::string(EngineToolchainFingerprint())};
}

Result<void> CheckHostCompatibility(const ModuleManifest& manifest, const HostInfo& host)
{
    if (!manifest.engine.Matches(host.engineVersion))
    {
        return MakeError(ErrorCode::VersionMismatch,
                         std::format("module '{}' needs engine '{}', this is {}", manifest.id, manifest.engine.Text(),
                                     host.engineVersion.ToString()));
    }
    if (!manifest.toolchain.empty() && manifest.toolchain != host.toolchain)
    {
        return MakeError(ErrorCode::VersionMismatch,
                         std::format("module '{}' was built with toolchain '{}', the engine with '{}'; rebuild it "
                                     "against this engine",
                                     manifest.id, manifest.toolchain, host.toolchain));
    }
    return {};
}

} // namespace interi::sdk
