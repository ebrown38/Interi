// SPDX-License-Identifier: MIT
#include <interi/loc/Localization.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <set>

namespace interi::loc
{

namespace
{

using Json = nlohmann::json;

constexpr std::string_view kLanguageAction = "language.set:";

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsAlpha(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

} // namespace

bool IsValidKey(std::string_view key) noexcept
{
    return !key.empty() &&
           std::ranges::all_of(key,
                               [](char c) { return IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-'; });
}

bool IsValidLanguage(std::string_view language) noexcept
{
    std::size_t start = 0;
    bool first = true;
    while (start <= language.size())
    {
        std::size_t end = language.find('-', start);
        if (end == std::string_view::npos)
        {
            end = language.size();
        }
        const std::string_view part = language.substr(start, end - start);
        if (first)
        {
            if (part.size() < 2 || part.size() > 3 || !std::ranges::all_of(part, IsAlpha))
            {
                return false;
            }
        }
        else if (part.size() < 2 || part.size() > 8 ||
                 !std::ranges::all_of(part, [](char c) { return IsAlpha(c) || IsDigit(c); }))
        {
            return false;
        }
        first = false;
        start = end + 1;
    }
    return true;
}

Result<StringTable> ParseStringTable(std::string_view text)
{
    const Json document = Json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer())
    {
        return Invalid("string table: not a versioned JSON object");
    }
    if (document["version"].get<int>() != 1)
    {
        return MakeError(ErrorCode::VersionMismatch, "string table: unsupported version");
    }
    if (!document.contains("language") || !document["language"].is_string())
    {
        return Invalid("string table: missing \"language\"");
    }
    StringTable table;
    table.language = document["language"].get<std::string>();
    if (!IsValidLanguage(table.language))
    {
        return Invalid(std::format("string table: bad language '{}'", table.language));
    }
    if (!document.contains("strings") || !document["strings"].is_object())
    {
        return Invalid("string table: missing \"strings\"");
    }
    for (const auto& [key, value] : document["strings"].items())
    {
        if (!IsValidKey(key) || !value.is_string())
        {
            return Invalid(std::format(
                "string table: bad entry '{}' (keys use letters, digits and ._-; values are strings)", key));
        }
        table.strings.emplace(key, value.get<std::string>());
    }
    return table;
}

std::string WriteStringTable(const StringTable& table)
{
    Json strings = Json::object();
    for (const auto& [key, value] : table.strings)
    {
        strings[key] = value;
    }
    return Json{{"version", 1}, {"language", table.language}, {"strings", std::move(strings)}}.dump(2) + "\n";
}

Localization::Localization(std::string fallbackLanguage)
    : m_fallback(std::move(fallbackLanguage))
    , m_language(m_fallback)
{
}

Result<void> Localization::AddTable(std::string_view provider, StringTable table)
{
    if (!IsValidLanguage(table.language))
    {
        return Invalid(std::format("bad language '{}'", table.language));
    }
    for (const auto& [key, value] : table.strings)
    {
        if (!IsValidKey(key))
        {
            return Invalid(std::format("bad string key '{}'", key));
        }
    }
    m_tables.push_back({.provider = std::string(provider), .table = std::move(table)});
    return {};
}

void Localization::RemoveProvider(std::string_view provider)
{
    std::erase_if(m_tables, [&](const Entry& entry) { return entry.provider == provider; });
}

Result<void> Localization::SetLanguage(std::string_view language)
{
    const auto has = std::ranges::any_of(m_tables, [&](const Entry& entry)
                                         { return entry.table.language == language && !entry.table.strings.empty(); });
    if (!has)
    {
        return MakeError(ErrorCode::NotFound, std::format("no strings for language '{}'", language));
    }
    m_language = language;
    return {};
}

std::vector<std::string> Localization::Languages() const
{
    std::set<std::string> languages;
    for (const Entry& entry : m_tables)
    {
        if (!entry.table.strings.empty())
        {
            languages.insert(entry.table.language);
        }
    }
    return {languages.begin(), languages.end()};
}

std::optional<std::string_view> Localization::FindIn(std::string_view language, std::string_view key) const noexcept
{
    for (const Entry& entry : std::views::reverse(m_tables))
    {
        if (entry.table.language != language)
        {
            continue;
        }
        if (const auto found = entry.table.strings.find(key); found != entry.table.strings.end())
        {
            return std::string_view(found->second);
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> Localization::Find(std::string_view key) const noexcept
{
    if (auto text = FindIn(m_language, key))
    {
        return text;
    }
    return m_language == m_fallback ? std::nullopt : FindIn(m_fallback, key);
}

std::string Localization::Translate(std::string_view key) const
{
    const auto text = Find(key);
    return std::string(text.value_or(key));
}

std::string Localization::Format(std::string_view key,
                                 std::span<const std::pair<std::string_view, std::string_view>> args) const
{
    const std::string text = Translate(key);
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{')
        {
            out += '{';
            ++i;
            continue;
        }
        const std::size_t close = text[i] == '{' ? text.find('}', i) : std::string::npos;
        if (close == std::string::npos)
        {
            out += text[i];
            continue;
        }
        const std::string_view name = std::string_view(text).substr(i + 1, close - i - 1);
        const auto arg = std::ranges::find(args, name, &std::pair<std::string_view, std::string_view>::first);
        if (arg != args.end())
        {
            out += arg->second;
        }
        else
        {
            out.append(text, i, close - i + 1);
        }
        i = close;
    }
    return out;
}

std::vector<std::string> Localization::MissingKeys(std::string_view language) const
{
    std::set<std::string> all;
    for (const Entry& entry : m_tables)
    {
        for (const auto& [key, value] : entry.table.strings)
        {
            all.insert(key);
        }
    }
    std::vector<std::string> missing;
    for (const std::string& key : all)
    {
        if (!FindIn(language, key))
        {
            missing.push_back(key);
        }
    }
    return missing;
}

bool ApplyLanguageAction(Localization& localization, std::string_view action)
{
    if (!action.starts_with(kLanguageAction))
    {
        return false;
    }
    return localization.SetLanguage(action.substr(kLanguageAction.size())).has_value();
}

Result<void> LoadModuleStrings(const sdk::ModuleManifest& manifest, const IFileSystem& moduleFiles,
                               Localization& localization)
{
    for (const std::string& path : manifest.localization)
    {
        INTERI_TRY_ASSIGN(bytes, moduleFiles.ReadFile(path));
        auto table = ParseStringTable({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        if (!table)
        {
            return MakeError(table.error().code, std::format("{}: {}", path, table.error().message));
        }
        INTERI_TRY(localization.AddTable(manifest.id, std::move(*table)));
    }
    return {};
}

} // namespace interi::loc
