// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/runtime/SaveGame.h>

#include <algorithm>
#include <format>
#include <utility>

namespace interi
{

namespace
{

constexpr FourCC kSaveMagic = MakeFourCC("ISAV");
constexpr FourCC kPayloadMagic = MakeFourCC("ISVP");
constexpr std::uint32_t kSaveVersion = 1;
constexpr std::uint32_t kMaxSections = 4096;

} // namespace

// Found by ADL from the archive.
template <typename Archive>
void Serialize(Archive& archive, SaveSection& section)
{
    archive(section.id, section.version, section.data);
}

const SaveSection* SaveGame::Find(std::string_view id) const noexcept
{
    const auto found = std::ranges::lower_bound(sections, id, {}, &SaveSection::id);
    return found != sections.end() && found->id == id ? &*found : nullptr;
}

void SaveGame::Set(SaveSection section)
{
    const auto at = std::ranges::lower_bound(sections, section.id, {}, &SaveSection::id);
    if (at != sections.end() && at->id == section.id)
    {
        *at = std::move(section);
    }
    else
    {
        sections.insert(at, std::move(section));
    }
}

std::vector<std::byte> WriteSave(const SaveGame& game)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kPayloadMagic, kSaveVersion);
        std::string label = game.label;
        std::vector<SaveSection> sections = game.sections;
        writer(label, sections);
    }
    std::uint64_t hash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kSaveMagic, kSaveVersion);
    writer(hash, payload);
    return file;
}

Result<SaveGame> ReadSave(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kSaveMagic, kSaveVersion));
    std::uint64_t hash = 0;
    std::vector<std::byte> payload;
    header(hash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != hash)
    {
        return MakeError(ErrorCode::Corrupt, "save payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kPayloadMagic, kSaveVersion));
    SaveGame game;
    reader(game.label, game.sections);
    INTERI_TRY(reader.Finish());
    if (game.sections.size() > kMaxSections)
    {
        return MakeError(ErrorCode::Corrupt, "save has too many sections");
    }
    for (std::size_t i = 0; i < game.sections.size(); ++i)
    {
        if (game.sections[i].id.empty() || (i > 0 && !(game.sections[i - 1].id < game.sections[i].id)))
        {
            return MakeError(ErrorCode::Corrupt, "save sections are not sorted and unique");
        }
    }
    return game;
}

Result<void> SaveRegistry::Add(std::string_view provider, SaveSectionDef def)
{
    if (def.id.empty() || !def.capture || !def.restore || def.version == 0)
    {
        return MakeError(ErrorCode::InvalidArgument, "a save section needs an id, a version and both functions");
    }
    if (provider != kEngineSaveProvider)
    {
        const std::string prefix = std::string(provider) + ":";
        if (!def.id.starts_with(prefix) || def.id.size() == prefix.size())
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("save section '{}' must be named '{}<name>'", def.id, prefix));
        }
    }
    else if (def.id.find(':') != std::string::npos)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("engine save section '{}' must not contain ':'", def.id));
    }
    const auto at =
        std::ranges::lower_bound(m_entries, def.id, {}, [](const Entry& e) -> const std::string& { return e.def.id; });
    if (at != m_entries.end() && at->def.id == def.id)
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("save section '{}' is already registered by '{}'", def.id, at->provider));
    }
    m_entries.insert(at, Entry{.provider = std::string(provider), .def = std::move(def)});
    return {};
}

std::size_t SaveRegistry::RemoveProvider(std::string_view provider)
{
    return std::erase_if(m_entries, [&](const Entry& entry) { return entry.provider == provider; });
}

SaveRegistry::Extracted SaveRegistry::ExtractProvider(std::string_view provider)
{
    Extracted extracted{.provider = std::string(provider), .sections = {}};
    for (Entry& entry : m_entries)
    {
        if (entry.provider == provider)
        {
            extracted.sections.push_back(std::move(entry.def));
        }
    }
    (void)RemoveProvider(provider);
    return extracted;
}

void SaveRegistry::Restore(Extracted extracted)
{
    for (SaveSectionDef& def : extracted.sections)
    {
        if (auto added = Add(extracted.provider, std::move(def)); !added)
        {
            INTERI_LOG_WARN("Save", "can't restore save section: {}", added.error().message);
        }
    }
}

std::vector<std::string> SaveRegistry::Ids() const
{
    std::vector<std::string> ids;
    ids.reserve(m_entries.size());
    for (const Entry& entry : m_entries)
    {
        ids.push_back(entry.def.id);
    }
    return ids;
}

bool SaveRegistry::Has(std::string_view id) const noexcept
{
    return std::ranges::any_of(m_entries, [&](const Entry& entry) { return entry.def.id == id; });
}

SaveGame SaveRegistry::Capture(std::string label, const SaveGame* previous) const
{
    SaveGame game;
    game.label = std::move(label);
    if (previous != nullptr)
    {
        for (const SaveSection& section : previous->sections)
        {
            if (!Has(section.id))
            {
                game.Set(section);
            }
        }
    }
    for (const Entry& entry : m_entries)
    {
        game.Set({.id = entry.def.id, .version = entry.def.version, .data = entry.def.capture()});
    }
    return game;
}

Result<void> SaveRegistry::Validate(const SaveGame& game) const
{
    for (const SaveSection& section : game.sections)
    {
        const auto entry =
            std::ranges::find(m_entries, section.id, [](const Entry& e) -> const std::string& { return e.def.id; });
        if (entry != m_entries.end() && section.version > entry->def.version)
        {
            return MakeError(ErrorCode::VersionMismatch,
                             std::format("save section '{}' is version {}, newer than the {} this build reads",
                                         section.id, section.version, entry->def.version));
        }
    }
    return {};
}

Result<RestoreReport> SaveRegistry::Apply(const SaveGame& game) const
{
    INTERI_TRY(Validate(game));
    RestoreReport report;
    std::vector<std::pair<const Entry*, const SaveSection*>> work;
    for (const SaveSection& section : game.sections)
    {
        const auto entry =
            std::ranges::find(m_entries, section.id, [](const Entry& e) -> const std::string& { return e.def.id; });
        if (entry == m_entries.end())
        {
            report.unknown.push_back(section.id);
        }
        else
        {
            work.emplace_back(&*entry, &section);
        }
    }
    for (const auto& [entry, section] : work)
    {
        if (auto done = entry->def.restore(section->data, section->version))
        {
            report.restored.push_back(section->id);
        }
        else
        {
            report.failed.push_back(std::format("{}: {}", section->id, done.error().message));
        }
    }
    return report;
}

SaveSlots::SaveSlots(IFileSystem& files, std::string folder, std::uint32_t slotCount)
    : m_files(&files)
    , m_folder(std::move(folder))
    , m_count(slotCount)
{
}

std::string SaveSlots::PathOf(std::uint32_t slot) const
{
    return std::format("{}/slot{}.isav", m_folder, slot);
}

Result<void> SaveSlots::Write(std::uint32_t slot, const SaveGame& game)
{
    if (slot >= m_count)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("slot {} is past the {} slots", slot, m_count));
    }
    const std::vector<std::byte> bytes = WriteSave(game);
    return m_files->WriteFile(PathOf(slot), bytes);
}

Result<SaveGame> SaveSlots::Read(std::uint32_t slot) const
{
    if (slot >= m_count)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("slot {} is past the {} slots", slot, m_count));
    }
    INTERI_TRY_ASSIGN(bytes, m_files->ReadFile(PathOf(slot)));
    return ReadSave(bytes);
}

bool SaveSlots::Exists(std::uint32_t slot) const
{
    return slot < m_count && m_files->Exists(PathOf(slot));
}

std::vector<SaveSlots::Info> SaveSlots::List() const
{
    std::vector<Info> slots;
    for (std::uint32_t slot = 0; slot < m_count; ++slot)
    {
        Info info{.slot = slot, .occupied = false, .corrupt = false, .label = {}};
        if (Exists(slot))
        {
            auto game = Read(slot);
            info.occupied = game.has_value();
            info.corrupt = !game.has_value();
            if (game)
            {
                info.label = game->label;
            }
        }
        slots.push_back(std::move(info));
    }
    return slots;
}

} // namespace interi
