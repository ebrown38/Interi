// SPDX-License-Identifier: MIT
#pragma once

// Editor services without UI: preferences, autosave timing and the log buffer the console panel shows.

#include <interi/core/Log.h>
#include <interi/core/Result.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// The user's editor settings, kept between sessions (editor.json in the user's config folder).
struct Preferences
{
    std::vector<std::string> recentProjects; ///< Project folders, most recent first.
    bool autosave = true;
    std::uint32_t autosaveSeconds = 120;
    bool keepPlayValues = true; ///< Keep changes made during play (chain Values and edits) when it stops.
    bool profilerFrameMarks = false;
    bool snap = true;
    float moveSnap = 0.25f;
    float rotateSnapDegrees = 15.0f;
    float sizeSnap = 0.1f;

    friend bool operator==(const Preferences&, const Preferences&) = default;
};

inline constexpr std::size_t kMaxRecentProjects = 10;

/// Unknown keys are ignored and missing ones keep their defaults (preferences are never worth refusing to start
/// over); InvalidArgument only for text that isn't a JSON object.
[[nodiscard]] Result<Preferences> ParsePreferences(std::string_view json);
[[nodiscard]] std::string WritePreferences(const Preferences& preferences);
/// Reads preferences; a missing file gives the defaults.
[[nodiscard]] Result<Preferences> LoadPreferences(const std::filesystem::path& path);
[[nodiscard]] Result<void> SavePreferences(const std::filesystem::path& path, const Preferences& preferences);
/// `$XDG_CONFIG_HOME/interi/editor.json` (else `~/.config/...`) on Linux, `%APPDATA%\Interi\editor.json` on
/// Windows, or editor.json in the temp folder if neither is set.
[[nodiscard]] std::filesystem::path DefaultPreferencesPath();
/// Moves (or adds) a project to the front of the recent list, which keeps kMaxRecentProjects.
void AddRecentProject(Preferences& preferences, std::string directory);

/// Decides when to autosave: once `interval` has passed since the last save (or autosave) while the document is
/// dirty. Time comes from the caller.
class AutosaveTimer
{
public:
    explicit AutosaveTimer(std::chrono::seconds interval) noexcept
        : m_interval(interval)
    {
    }

    /// Adds elapsed time; true when an autosave is due now (the timer then restarts).
    bool Tick(std::chrono::nanoseconds elapsed, bool dirty) noexcept;
    /// Restarts the interval (after a save).
    void Reset() noexcept { m_elapsed = {}; }
    void SetInterval(std::chrono::seconds interval) noexcept { m_interval = interval; }

private:
    std::chrono::seconds m_interval;
    std::chrono::nanoseconds m_elapsed{};
};

/// Where a scene autosaves: "<path>.autosave" next to it, or "autosave/untitled.scene.json" for an unnamed one.
[[nodiscard]] std::string AutosavePath(std::string_view scenePath);

/// The last `capacity` log records, for the console panel. Thread-safe: LogBufferSink adds on the logger's flush
/// thread while the UI reads.
class LogBuffer
{
public:
    struct Record
    {
        LogLevel level = LogLevel::Info;
        std::uint64_t timestampNs = 0;
        std::string category;
        std::string message;
    };

    explicit LogBuffer(std::size_t capacity = 2000)
        : m_capacity(capacity)
    {
    }

    void Add(const LogRecord& record);

    /// A copy of the kept records, oldest first.
    [[nodiscard]] std::vector<Record> Records() const;
    /// Records written since construction (keeps counting past the capacity).
    [[nodiscard]] std::uint64_t TotalWritten() const;
    void Clear();

private:
    mutable std::mutex m_mutex;
    std::deque<Record> m_records;
    std::size_t m_capacity;
    std::uint64_t m_total = 0;
};

/// Feeds a LogBuffer from the logger (Logger::AddSink takes ownership of the sink; the buffer is shared).
class LogBufferSink final : public ILogSink
{
public:
    explicit LogBufferSink(std::shared_ptr<LogBuffer> buffer) noexcept
        : m_buffer(std::move(buffer))
    {
    }

    void Write(const LogRecord& record) override { m_buffer->Add(record); }

private:
    std::shared_ptr<LogBuffer> m_buffer;
};

} // namespace interi::editor
