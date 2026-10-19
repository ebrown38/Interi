// SPDX-License-Identifier: MIT
#include <interi/editor/ModuleManager.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace interi::editor
{

namespace
{

using Json = nlohmann::json;

const sdk::ModuleManifest* FindManifest(std::span<const sdk::ModuleManifest> manifests, std::string_view id)
{
    const auto it = std::ranges::find_if(manifests, [&](const sdk::ModuleManifest& m) { return m.id == id; });
    return it == manifests.end() ? nullptr : &*it;
}

std::string Join(const std::vector<std::string>& items)
{
    std::string text;
    for (const std::string& item : items)
    {
        text += text.empty() ? item : ", " + item;
    }
    return text;
}

/// The loaded modules (any status) that require `id`.
std::vector<std::string> RequiredBy(const sdk::ModuleGraph& graph, std::string_view id)
{
    std::vector<std::string> result;
    for (const sdk::ResolvedModule& module : graph.modules)
    {
        const bool needs =
            std::ranges::any_of(module.manifest.dependencies, [&](const sdk::ModuleDependency& dependency)
                                { return dependency.id == id && !dependency.optional; });
        if (needs)
        {
            result.push_back(module.manifest.id);
        }
    }
    return result;
}

std::string EngineId(const sdk::ModuleGraph& graph)
{
    return graph.physicsEngine == sdk::ModuleGraph::npos ? std::string{}
                                                         : graph.modules[graph.physicsEngine].manifest.id;
}

std::vector<std::string> LoadedIds(const sdk::ModuleGraph& graph)
{
    std::vector<std::string> ids;
    for (const sdk::ResolvedModule& module : graph.modules)
    {
        ids.push_back(module.manifest.id);
    }
    std::ranges::sort(ids);
    return ids;
}

ModuleEntry MakeEntry(const sdk::ModuleManifest& manifest, const TrustStore& trust)
{
    ModuleEntry entry;
    entry.id = manifest.id;
    entry.name = manifest.name;
    entry.author = manifest.author;
    entry.license = manifest.license;
    entry.description = manifest.description;
    entry.version = manifest.version;
    entry.kind = manifest.kind;
    entry.nativeCode = !manifest.library.empty();
    entry.trusted = trust.IsTrusted(manifest);
    return entry;
}

/// Resolves `after` and describes the difference from `before`.
Result<ModuleChange> Describe(const sdk::Project& before, sdk::Project after,
                              std::span<const sdk::ModuleManifest> manifests, const sdk::HostInfo& host,
                              const TrustStore& trust)
{
    INTERI_TRY_ASSIGN(graph, sdk::ResolveModuleGraph(after.modules, manifests, host, after.physicsEngine));
    ModuleChange change;
    std::vector<std::string> previous;
    if (auto old = sdk::ResolveModuleGraph(before.modules, manifests, host, before.physicsEngine))
    {
        previous = LoadedIds(*old);
        change.physicsEngineBefore = EngineId(*old);
    }
    const std::vector<std::string> now = LoadedIds(graph);
    std::ranges::set_difference(now, previous, std::back_inserter(change.added));
    std::ranges::set_difference(previous, now, std::back_inserter(change.removed));
    change.physicsEngineAfter = EngineId(graph);
    for (const std::string& id : change.added)
    {
        if (const sdk::ModuleManifest* manifest = FindManifest(manifests, id);
            manifest != nullptr && NeedsTrust(*manifest, trust))
        {
            change.needsTrust.push_back(id);
        }
    }
    change.project = std::move(after);
    return change;
}

} // namespace

bool TrustStore::IsTrusted(const sdk::ModuleManifest& manifest) const noexcept
{
    return std::ranges::any_of(
        m_entries, [&](const Entry& entry)
        { return entry.id == manifest.id && entry.author == manifest.author && entry.license == manifest.license; });
}

void TrustStore::Trust(const sdk::ModuleManifest& manifest)
{
    Revoke(manifest.id);
    const auto position = std::ranges::find_if(m_entries, [&](const Entry& entry) { return entry.id > manifest.id; });
    m_entries.insert(position, {.id = manifest.id, .author = manifest.author, .license = manifest.license});
}

void TrustStore::Revoke(std::string_view id)
{
    std::erase_if(m_entries, [&](const Entry& entry) { return entry.id == id; });
}

Result<TrustStore> TrustStore::Parse(std::string_view text)
{
    const Json json = Json::parse(text, nullptr, false);
    if (!json.is_object() || !json.contains("trusted") || !json["trusted"].is_array())
    {
        return MakeError(ErrorCode::InvalidArgument, "trusted modules: expected {\"trusted\": [...]}");
    }
    TrustStore store;
    for (const Json& item : json["trusted"])
    {
        if (!item.is_object() || !item.value("id", Json()).is_string() || !item.value("author", Json()).is_string() ||
            !item.value("license", Json()).is_string())
        {
            return MakeError(ErrorCode::InvalidArgument, "trusted modules: each entry needs id, author and license");
        }
        store.m_entries.push_back({.id = item["id"].get<std::string>(),
                                   .author = item["author"].get<std::string>(),
                                   .license = item["license"].get<std::string>()});
    }
    std::ranges::sort(store.m_entries, {}, &Entry::id);
    return store;
}

std::string TrustStore::Write() const
{
    Json list = Json::array();
    for (const Entry& entry : m_entries)
    {
        list.push_back({{"id", entry.id}, {"author", entry.author}, {"license", entry.license}});
    }
    return Json{{"trusted", list}}.dump(2) + "\n";
}

Result<TrustStore> TrustStore::Load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return TrustStore{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return Parse(text.str());
}

Result<void> TrustStore::Save(const std::filesystem::path& path) const
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string text = Write();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file)
    {
        return MakeError(ErrorCode::IoError, "can't write " + path.string());
    }
    return {};
}

std::filesystem::path TrustStorePath(const std::filesystem::path& preferencesPath)
{
    return preferencesPath.parent_path() / "trusted_modules.json";
}

bool NeedsTrust(const sdk::ModuleManifest& manifest, const TrustStore& store) noexcept
{
    return !manifest.library.empty() && !store.IsTrusted(manifest);
}

std::string_view ToString(ModuleStatus status) noexcept
{
    switch (status)
    {
    case ModuleStatus::Enabled:
        return "Enabled";
    case ModuleStatus::Implicit:
        return "Implicit";
    case ModuleStatus::Available:
        return "Available";
    case ModuleStatus::Incompatible:
        return "Incompatible";
    case ModuleStatus::Engine:
        return "Engine";
    }
    return "Available";
}

const ModuleEntry* ModuleManagerModel::Find(std::string_view id) const noexcept
{
    const auto it = std::ranges::find(entries, id, &ModuleEntry::id);
    return it == entries.end() ? nullptr : &*it;
}

ModuleManagerModel BuildModuleManagerModel(const sdk::Project& project, std::span<const sdk::ModuleManifest> manifests,
                                           const sdk::HostInfo& host, const TrustStore& trust)
{
    ModuleManagerModel model;
    auto resolved = sdk::ResolveModuleGraph(project.modules, manifests, host, project.physicsEngine);
    sdk::ModuleGraph graph;
    if (resolved)
    {
        graph = std::move(*resolved);
    }
    else
    {
        model.graphError = resolved.error().message;
    }

    std::vector<ModuleEntry> enabled;
    std::vector<ModuleEntry> implicit;
    std::vector<ModuleEntry> engines;
    std::vector<ModuleEntry> available;
    std::vector<ModuleEntry> incompatible;

    for (const sdk::ModuleManifest& manifest : manifests)
    {
        ModuleEntry entry = MakeEntry(manifest, trust);
        const sdk::ResolvedModule* loaded = graph.Get(manifest.id);
        entry.requiredBy = RequiredBy(graph, manifest.id);
        const auto listed = std::ranges::find(project.modules, manifest.id);
        if (manifest.kind == sdk::ModuleKind::PhysicsEngine)
        {
            entry.status = ModuleStatus::Engine;
            entry.reason = loaded != nullptr ? "the selected Physics Engine" : "not selected";
            engines.push_back(std::move(entry));
        }
        else if (listed != project.modules.end())
        {
            entry.status = ModuleStatus::Enabled;
            entry.position = static_cast<int>(listed - project.modules.begin());
            enabled.push_back(std::move(entry));
        }
        else if (loaded != nullptr)
        {
            entry.status = ModuleStatus::Implicit;
            entry.reason = "required by " + Join(entry.requiredBy);
            implicit.push_back(std::move(entry));
        }
        else if (auto refused = sdk::CanAddModule(project, manifests, manifest, host); !refused)
        {
            entry.status = ModuleStatus::Incompatible;
            entry.reason = refused.error().message;
            incompatible.push_back(std::move(entry));
        }
        else
        {
            available.push_back(std::move(entry));
        }
    }

    std::ranges::sort(enabled, {}, &ModuleEntry::position);
    const auto loadIndex = [&](const ModuleEntry& entry) { return graph.Find(entry.id); };
    std::ranges::sort(implicit, {}, loadIndex);
    for (auto* group : {&engines, &available, &incompatible})
    {
        std::ranges::sort(*group, {}, &ModuleEntry::id);
    }
    for (auto* group : {&enabled, &implicit, &engines, &available, &incompatible})
    {
        std::ranges::move(*group, std::back_inserter(model.entries));
    }
    return model;
}

Result<ModuleChange> EnableModule(const sdk::Project& project, std::span<const sdk::ModuleManifest> manifests,
                                  std::string_view id, const sdk::HostInfo& host, const TrustStore& trust,
                                  std::size_t position)
{
    const sdk::ModuleManifest* manifest = FindManifest(manifests, id);
    if (manifest == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("module '{}' is not installed", id));
    }
    if (manifest->kind == sdk::ModuleKind::PhysicsEngine)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("'{}' is a Physics Engine: it is selected by the modules that support it", id));
    }
    if (std::ranges::find(project.modules, id) != project.modules.end())
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("module '{}' is enabled already", id));
    }
    sdk::Project after = project;
    position = std::min(position, after.modules.size());
    after.modules.insert(after.modules.begin() + static_cast<std::ptrdiff_t>(position), std::string(id));
    return Describe(project, std::move(after), manifests, host, trust);
}

Result<ModuleChange> DisableModule(const sdk::Project& project, std::span<const sdk::ModuleManifest> manifests,
                                   std::string_view id, const sdk::HostInfo& host, const TrustStore& trust)
{
    const auto listed = std::ranges::find(project.modules, id);
    if (listed == project.modules.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("module '{}' is not enabled", id));
    }
    if (auto graph = sdk::ResolveModuleGraph(project.modules, manifests, host, project.physicsEngine))
    {
        std::vector<std::string> dependents = RequiredBy(*graph, id);
        if (!dependents.empty())
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("can't disable '{}': required by {}", id, Join(dependents)));
        }
    }
    sdk::Project after = project;
    after.modules.erase(after.modules.begin() + (listed - project.modules.begin()));
    return Describe(project, std::move(after), manifests, host, trust);
}

Result<ModuleChange> MoveModule(const sdk::Project& project, std::span<const sdk::ModuleManifest> manifests,
                                std::string_view id, std::size_t position, const sdk::HostInfo& host,
                                const TrustStore& trust)
{
    const auto listed = std::ranges::find(project.modules, id);
    if (listed == project.modules.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("module '{}' is not enabled", id));
    }
    sdk::Project after = project;
    after.modules.erase(after.modules.begin() + (listed - project.modules.begin()));
    position = std::min(position, after.modules.size());
    after.modules.insert(after.modules.begin() + static_cast<std::ptrdiff_t>(position), std::string(id));
    return Describe(project, std::move(after), manifests, host, trust);
}

} // namespace interi::editor
