// SPDX-License-Identifier: MIT
#include <interi/editor/EditorServices.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace interi::editor
{

namespace
{

using Json = nlohmann::json;

template <typename T>
void Read(const Json& json, std::string_view key, T& out)
{
    const auto found = json.find(key);
    if (found == json.end())
    {
        return;
    }
    if constexpr (std::is_same_v<T, bool>)
    {
        if (found->is_boolean())
        {
            out = found->get<bool>();
        }
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        if (found->is_number())
        {
            out = found->get<float>();
        }
    }
    else if constexpr (std::is_same_v<T, std::uint32_t>)
    {
        if (found->is_number_unsigned())
        {
            out = found->get<std::uint32_t>();
        }
    }
}

} // namespace

Result<Preferences> ParsePreferences(std::string_view text)
{
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        return MakeError(ErrorCode::InvalidArgument, "preferences: not a JSON object");
    }
    Preferences preferences;
    if (const auto recent = json.find("recentProjects"); recent != json.end() && recent->is_array())
    {
        for (const Json& entry : *recent)
        {
            if (entry.is_string() && preferences.recentProjects.size() < kMaxRecentProjects)
            {
                preferences.recentProjects.push_back(entry.get<std::string>());
            }
        }
    }
    Read(json, "autosave", preferences.autosave);
    Read(json, "autosaveSeconds", preferences.autosaveSeconds);
    preferences.autosaveSeconds = std::max<std::uint32_t>(preferences.autosaveSeconds, 10);
    Read(json, "keepPlayValues", preferences.keepPlayValues);
    Read(json, "profilerFrameMarks", preferences.profilerFrameMarks);
    Read(json, "snap", preferences.snap);
    Read(json, "moveSnap", preferences.moveSnap);
    Read(json, "rotateSnapDegrees", preferences.rotateSnapDegrees);
    Read(json, "sizeSnap", preferences.sizeSnap);
    return preferences;
}

std::string WritePreferences(const Preferences& preferences)
{
    const Json json = {{"recentProjects", preferences.recentProjects},
                       {"autosave", preferences.autosave},
                       {"autosaveSeconds", preferences.autosaveSeconds},
                       {"keepPlayValues", preferences.keepPlayValues},
                       {"profilerFrameMarks", preferences.profilerFrameMarks},
                       {"snap", preferences.snap},
                       {"moveSnap", preferences.moveSnap},
                       {"rotateSnapDegrees", preferences.rotateSnapDegrees},
                       {"sizeSnap", preferences.sizeSnap}};
    return json.dump(2) + "\n";
}

Result<Preferences> LoadPreferences(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return Preferences{};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParsePreferences(text.str());
}

Result<void> SavePreferences(const std::filesystem::path& path, const Preferences& preferences)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string text = WritePreferences(preferences);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file)
    {
        return MakeError(ErrorCode::IoError, "can't write " + path.string());
    }
    return {};
}

std::filesystem::path DefaultPreferencesPath()
{
#if defined(_WIN32)
#pragma warning(suppress : 4996)                  // getenv is fine here: read once, on the UI thread
    const char* appData = std::getenv("APPDATA"); // NOLINT(concurrency-mt-unsafe)
    if (appData != nullptr && *appData != '\0')
    {
        return std::filesystem::path(appData) / "Interi" / "editor.json";
    }
#else
    if (const char* config = std::getenv("XDG_CONFIG_HOME");
        config != nullptr && *config != '\0') // NOLINT(concurrency-mt-unsafe)
    {
        return std::filesystem::path(config) / "interi" / "editor.json";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') // NOLINT(concurrency-mt-unsafe)
    {
        return std::filesystem::path(home) / ".config" / "interi" / "editor.json";
    }
#endif
    return std::filesystem::temp_directory_path() / "interi_editor.json";
}

void AddRecentProject(Preferences& preferences, std::string directory)
{
    std::erase(preferences.recentProjects, directory);
    preferences.recentProjects.insert(preferences.recentProjects.begin(), std::move(directory));
    if (preferences.recentProjects.size() > kMaxRecentProjects)
    {
        preferences.recentProjects.resize(kMaxRecentProjects);
    }
}

bool AutosaveTimer::Tick(std::chrono::nanoseconds elapsed, bool dirty) noexcept
{
    if (!dirty)
    {
        m_elapsed = {};
        return false;
    }
    m_elapsed += elapsed;
    if (m_elapsed < m_interval)
    {
        return false;
    }
    m_elapsed = {};
    return true;
}

std::string AutosavePath(std::string_view scenePath)
{
    return scenePath.empty() ? std::string("autosave/untitled.scene.json") : std::string(scenePath) + ".autosave";
}

void LogBuffer::Add(const LogRecord& record)
{
    std::scoped_lock lock(m_mutex);
    m_records.push_back({.level = record.level,
                         .timestampNs = record.timestampNs,
                         .category = std::string(record.category),
                         .message = std::string(record.message)});
    ++m_total;
    while (m_records.size() > m_capacity)
    {
        m_records.pop_front();
    }
}

std::vector<LogBuffer::Record> LogBuffer::Records() const
{
    std::scoped_lock lock(m_mutex);
    return {m_records.begin(), m_records.end()};
}

std::uint64_t LogBuffer::TotalWritten() const
{
    std::scoped_lock lock(m_mutex);
    return m_total;
}

void LogBuffer::Clear()
{
    std::scoped_lock lock(m_mutex);
    m_records.clear();
}

} // namespace interi::editor
