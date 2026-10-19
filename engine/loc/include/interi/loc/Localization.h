// SPDX-License-Identifier: MIT
#pragma once

// Localization (M24.4): string tables per language, a language switch and key lookup with a fallback language.
//
//     { "version": 1, "language": "fr",
//       "strings": { "menu.play": "Jouer", "hud.coins": "Pièces {coins}/{total}" } }
//
// A table is one language's strings (UTF-8). Modules ship tables (module.json `localization`), the project can add its
// own, and several tables of one language merge: the table added last wins per key, so a higher module or a project
// retranslates a lower module's text. The HUD binds a string with `{@key}` (hud/HudLayout.h); a translation may itself
// contain `{valueId}` placeholders, which the HUD fills from Values.
//
// Lookup order: the current language, then the fallback (default "en"), then the key itself, so a missing translation
// shows its key rather than nothing. Language codes are BCP 47 style ("en", "pt-BR"), compared case-sensitively.

#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>
#include <interi/sdk/Manifest.h>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::loc
{

/// A string key: letters, digits and "._-", not empty.
[[nodiscard]] bool IsValidKey(std::string_view key) noexcept;
/// A language code: 2 to 3 letters, then optional "-" subtags of 2 to 8 letters or digits.
[[nodiscard]] bool IsValidLanguage(std::string_view language) noexcept;

struct StringTable
{
    std::string language;
    std::map<std::string, std::string, std::less<>> strings;
    friend bool operator==(const StringTable&, const StringTable&) = default;
};

/// InvalidArgument for malformed JSON, a bad language or key, or a non-string value; VersionMismatch for another
/// version.
Result<StringTable> ParseStringTable(std::string_view json);
/// Canonical text (sorted keys); parses back to the same table.
[[nodiscard]] std::string WriteStringTable(const StringTable& table);

class Localization
{
public:
    explicit Localization(std::string fallbackLanguage = "en");

    /// Adds `provider`'s table for its language; it wins over tables added earlier for the same keys. InvalidArgument
    /// for a bad language or key.
    Result<void> AddTable(std::string_view provider, StringTable table);
    /// Drops everything `provider` added (a module unloading); the current language stays even if it loses all text.
    void RemoveProvider(std::string_view provider);

    /// Switches language. NotFound when no table has that language (the current one is kept).
    Result<void> SetLanguage(std::string_view language);
    [[nodiscard]] const std::string& Language() const noexcept { return m_language; }
    [[nodiscard]] const std::string& Fallback() const noexcept { return m_fallback; }
    /// Every language with at least one string, sorted.
    [[nodiscard]] std::vector<std::string> Languages() const;

    /// The text for `key` in the current language, else the fallback's.
    [[nodiscard]] std::optional<std::string_view> Find(std::string_view key) const noexcept;
    /// Find, or the key itself.
    [[nodiscard]] std::string Translate(std::string_view key) const;
    /// Translate with `{name}` placeholders replaced from `args` (unknown names stay as written; `{{` is a literal
    /// brace).
    [[nodiscard]] std::string Format(std::string_view key,
                                     std::span<const std::pair<std::string_view, std::string_view>> args) const;
    /// Keys the fallback language or any other language has but `language` lacks, sorted: the to-do list of a
    /// translation.
    [[nodiscard]] std::vector<std::string> MissingKeys(std::string_view language) const;

private:
    struct Entry
    {
        std::string provider;
        StringTable table;
    };
    [[nodiscard]] std::optional<std::string_view> FindIn(std::string_view language,
                                                         std::string_view key) const noexcept;

    std::string m_fallback;
    std::string m_language;
    std::vector<Entry> m_tables; ///< Insertion order; later wins.
};

/// Handles the menu action `language.set:<code>` (so a settings menu button can switch language). False when `action`
/// is something else or the language is unknown (nothing changes then).
bool ApplyLanguageAction(Localization& localization, std::string_view action);

/// Loads the manifest's `localization` files (read from `moduleFiles`, rooted at the module's folder) as
/// `manifest.id`'s tables.
Result<void> LoadModuleStrings(const sdk::ModuleManifest& manifest, const IFileSystem& moduleFiles,
                               Localization& localization);

} // namespace interi::loc
