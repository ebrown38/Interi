// SPDX-License-Identifier: MIT
#pragma once

// Save games: a container of versioned sections, each contributed by the engine or a module, and numbered save
// slots on a file system.
//
// File ("ISAV"): the payload's XXH3 hash, then the payload, an archive ("ISVP") holding the label and the sections
// sorted by id. A flipped byte reads as Corrupt. A section is `id` ("<module id>:<name>"), the version its owner
// wrote it with and opaque bytes, so each owner migrates its own data with `archive.Version()`-style checks.
//
// SaveRegistry is where owners register capture/restore functions. A module registers through
// ModuleRegistrar::Saves() (the host removes its sections before the library unloads, and restores them if a reload
// fails). Sections the loaded modules don't know (a module that is not enabled) survive a load-and-save round trip
// when Capture is given the previous SaveGame.

#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{

struct SaveSection
{
    std::string id;
    std::uint32_t version = 1;
    std::vector<std::byte> data;
};

struct SaveGame
{
    std::string label;                 ///< Shown in slot lists ("Level 3, 12 coins").
    std::vector<SaveSection> sections; ///< Sorted by id, unique.

    [[nodiscard]] const SaveSection* Find(std::string_view id) const noexcept;
    /// Adds or replaces a section, keeping the order.
    void Set(SaveSection section);
};

[[nodiscard]] std::vector<std::byte> WriteSave(const SaveGame& game);
[[nodiscard]] Result<SaveGame> ReadSave(std::span<const std::byte> data);

/// One owner's capture and restore functions for one section.
struct SaveSectionDef
{
    std::string id;
    std::uint32_t version = 1; ///< What `capture` writes now; `restore` receives older versions too.
    std::function<std::vector<std::byte>()> capture;
    /// Applies the bytes a save holds. `savedVersion` <= `version`.
    std::function<Result<void>(std::span<const std::byte> data, std::uint32_t savedVersion)> restore;
};

struct RestoreReport
{
    std::vector<std::string> restored;
    std::vector<std::string> unknown; ///< In the save, but nothing registered (kept by Capture's `previous`).
    std::vector<std::string> failed;  ///< "<id>: reason" for sections whose restore returned an error.
};

class SaveRegistry
{
public:
    /// Registers `def` for `provider`. InvalidArgument for an empty id/function or a module section id that is not
    /// "<provider>:<name>"; AlreadyExists-style InvalidArgument for an id another provider registered.
    Result<void> Add(std::string_view provider, SaveSectionDef def);
    /// Removes every section `provider` registered. Returns how many.
    std::size_t RemoveProvider(std::string_view provider);

    struct Extracted
    {
        std::string provider;
        std::vector<SaveSectionDef> sections;
    };
    [[nodiscard]] Extracted ExtractProvider(std::string_view provider);
    void Restore(Extracted extracted);

    [[nodiscard]] std::vector<std::string> Ids() const;
    [[nodiscard]] bool Has(std::string_view id) const noexcept;

    /// Captures every registered section. Sections of `previous` that nothing registered are carried over.
    [[nodiscard]] SaveGame Capture(std::string label, const SaveGame* previous = nullptr) const;

    /// VersionMismatch when a section was written by a newer version than its owner knows (what Apply checks first).
    [[nodiscard]] Result<void> Validate(const SaveGame& game) const;

    /// Restores `game`. Nothing is applied when a section was written by a newer version than its owner knows
    /// (VersionMismatch naming it). Otherwise every known section is restored in id order; one that fails is
    /// reported and the others still apply.
    [[nodiscard]] Result<RestoreReport> Apply(const SaveGame& game) const;

private:
    struct Entry
    {
        std::string provider;
        SaveSectionDef def;
    };
    std::vector<Entry> m_entries; ///< Sorted by id.
};

inline constexpr std::string_view kEngineSaveProvider = "engine";

/// Numbered slots under `folder` ("saves/slot0.isav"...). `files` must outlive this.
class SaveSlots
{
public:
    SaveSlots(IFileSystem& files, std::string folder, std::uint32_t slotCount);

    [[nodiscard]] std::uint32_t Count() const noexcept { return m_count; }
    /// InvalidArgument for a slot past Count().
    Result<void> Write(std::uint32_t slot, const SaveGame& game);
    [[nodiscard]] Result<SaveGame> Read(std::uint32_t slot) const;
    [[nodiscard]] bool Exists(std::uint32_t slot) const;

    struct Info
    {
        std::uint32_t slot = 0;
        bool occupied = false;
        bool corrupt = false; ///< A file is there but doesn't read.
        std::string label;
    };
    /// Every slot, in order, for a load menu.
    [[nodiscard]] std::vector<Info> List() const;

private:
    [[nodiscard]] std::string PathOf(std::uint32_t slot) const;

    IFileSystem* m_files;
    std::string m_folder;
    std::uint32_t m_count;
};

} // namespace interi
