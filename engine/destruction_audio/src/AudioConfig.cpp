// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/destruction_audio/AudioConfig.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

namespace interi::destruction
{

namespace
{

constexpr std::string_view kWildcard = "*";

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

/// Reads the fields of one cue object, reporting with `where` (e.g. "impacts[2]").
class CueReader
{
public:
    CueReader(const nlohmann::json& object, std::string where, std::vector<std::string>& warnings)
        : m_object(object)
        , m_where(std::move(where))
        , m_warnings(warnings)
    {
    }

    Result<> Number(std::string_view key, float& out)
    {
        m_known.emplace_back(key);
        const auto it = m_object.find(key);
        if (it == m_object.end())
        {
            m_warnings.push_back(std::format("{}: missing \"{}\", using default {}", m_where, key, out));
            return {};
        }
        if (!it->is_number() ||
            !(std::abs(it->get<double>()) <= static_cast<double>(std::numeric_limits<float>::max())))
        {
            return Invalid(std::format("{}: \"{}\" is not a float", m_where, key));
        }
        out = static_cast<float>(it->get<double>());
        return {};
    }

    Result<std::vector<std::string>> Strings(std::string_view key)
    {
        m_known.emplace_back(key);
        const auto it = m_object.find(key);
        if (it == m_object.end() || !it->is_array() || it->empty())
        {
            return Invalid(std::format("{}: \"{}\" must be a non-empty array of strings", m_where, key));
        }
        std::vector<std::string> strings;
        for (const nlohmann::json& value : *it)
        {
            if (!value.is_string() || value.get<std::string>().empty())
            {
                return Invalid(std::format("{}: \"{}\" must be a non-empty array of strings", m_where, key));
            }
            strings.push_back(value.get<std::string>());
        }
        return strings;
    }

    Result<> Cue(AudioCue& cue)
    {
        INTERI_TRY_ASSIGN(clips, Strings("clips"));
        cue.clips = std::move(clips);
        INTERI_TRY(Number("volume", cue.volume));
        INTERI_TRY(Number("priority", cue.priority));
        INTERI_TRY(Number("minDistance", cue.attenuation.minDistance));
        INTERI_TRY(Number("maxDistance", cue.attenuation.maxDistance));
        INTERI_TRY(Number("rolloff", cue.attenuation.rolloff));
        return {};
    }

    void WarnUnknown()
    {
        for (const auto& [key, value] : m_object.items())
        {
            if (std::ranges::find(m_known, key) == m_known.end())
            {
                m_warnings.push_back(std::format("{}: unknown field \"{}\" ignored", m_where, key));
            }
        }
    }

    [[nodiscard]] Result<> Check(std::string_view field, float value, bool ok) const
    {
        if (!std::isfinite(value) || !ok)
        {
            return Invalid(std::format("{}: invalid \"{}\" ({})", m_where, field, value));
        }
        return {};
    }

    [[nodiscard]] Result<> CheckCue(const AudioCue& cue) const
    {
        INTERI_TRY(Check("volume", cue.volume, cue.volume >= 0.0f));
        INTERI_TRY(Check("priority", cue.priority, cue.priority >= 0.0f));
        INTERI_TRY(Check("minDistance", cue.attenuation.minDistance, cue.attenuation.minDistance > 0.0f));
        INTERI_TRY(Check("maxDistance", cue.attenuation.maxDistance,
                         cue.attenuation.maxDistance > cue.attenuation.minDistance));
        INTERI_TRY(Check("rolloff", cue.attenuation.rolloff, cue.attenuation.rolloff >= 0.0f));
        return {};
    }

private:
    const nlohmann::json& m_object;
    std::string m_where;
    std::vector<std::string>& m_warnings;
    std::vector<std::string> m_known;
};

Result<> ParseObject(const nlohmann::json& root, std::string_view key, std::vector<std::string>& warnings,
                     const auto& parse)
{
    const auto it = root.find(key);
    if (it == root.end() || !it->is_object())
    {
        return Invalid(std::format("destruction audio file has no \"{}\" object", key));
    }
    CueReader reader(*it, std::string(key), warnings);
    INTERI_TRY(parse(reader));
    reader.WarnUnknown();
    return {};
}

float LogRamp(float value, float min, float full) noexcept
{
    if (!(value >= min) || !(value > 0.0f))
    {
        return 0.0f;
    }
    if (!(full > min) || !(min > 0.0f))
    {
        return 1.0f;
    }
    return std::clamp(std::log(value / min) / std::log(full / min), 0.0f, 1.0f);
}

} // namespace

Result<DestructionAudioConfigSet> ParseDestructionAudioConfig(std::string_view json)
{
    const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("destruction audio file is not a JSON object");
    }
    DestructionAudioConfigSet set;
    DestructionAudioConfig& config = set.config;
    const auto impacts = root.find("impacts");
    if (impacts == root.end() || !impacts->is_array())
    {
        return Invalid("destruction audio file has no \"impacts\" array");
    }
    for (std::size_t i = 0; i < impacts->size(); ++i)
    {
        const nlohmann::json& entry = (*impacts)[i];
        const std::string where = std::format("impacts[{}]", i);
        if (!entry.is_object())
        {
            return Invalid(std::format("{} is not an object", where));
        }
        CueReader reader(entry, where, set.warnings);
        ImpactBank bank;
        INTERI_TRY_ASSIGN(materials, reader.Strings("materials"));
        if (materials.size() != 2)
        {
            return Invalid(std::format("{}: \"materials\" must hold two names", where));
        }
        bank.materialA = materials[0];
        bank.materialB = materials[1];
        bank.idA = bank.materialA == kWildcard ? StringId() : StringId(bank.materialA);
        bank.idB = bank.materialB == kWildcard ? StringId() : StringId(bank.materialB);
        INTERI_TRY(reader.Cue(bank));
        INTERI_TRY(reader.Number("minImpulse", bank.minImpulse));
        INTERI_TRY(reader.Number("fullImpulse", bank.fullImpulse));
        INTERI_TRY(reader.Number("minVolume", bank.minVolume));
        reader.WarnUnknown();
        INTERI_TRY(reader.CheckCue(bank));
        INTERI_TRY(reader.Check("minImpulse", bank.minImpulse, bank.minImpulse >= 0.0f));
        INTERI_TRY(reader.Check("fullImpulse", bank.fullImpulse, bank.fullImpulse > bank.minImpulse));
        INTERI_TRY(reader.Check("minVolume", bank.minVolume, bank.minVolume >= 0.0f && bank.minVolume <= 1.0f));
        config.impacts.push_back(std::move(bank));
    }
    INTERI_TRY(ParseObject(root, "creak", set.warnings,
                           [&](CueReader& reader) -> Result<>
                           {
                               INTERI_TRY(reader.Cue(config.creak));
                               return reader.CheckCue(config.creak);
                           }));
    INTERI_TRY(ParseObject(root, "rumble", set.warnings,
                           [&](CueReader& reader) -> Result<>
                           {
                               RumbleCue& rumble = config.rumble;
                               INTERI_TRY(reader.Cue(rumble));
                               INTERI_TRY(reader.Number("minMass", rumble.minMass));
                               INTERI_TRY(reader.Number("fullMass", rumble.fullMass));
                               INTERI_TRY(reader.CheckCue(rumble));
                               INTERI_TRY(reader.Check("minMass", rumble.minMass, rumble.minMass >= 0.0f));
                               return reader.Check("fullMass", rumble.fullMass, rumble.fullMass > rumble.minMass);
                           }));
    INTERI_TRY(ParseObject(root, "explosion", set.warnings,
                           [&](CueReader& reader) -> Result<>
                           {
                               INTERI_TRY(reader.Cue(config.explosion));
                               return reader.CheckCue(config.explosion);
                           }));
    for (const auto& [key, value] : root.items())
    {
        if (key != "impacts" && key != "creak" && key != "rumble" && key != "explosion")
        {
            set.warnings.push_back(std::format("destruction audio: unknown field \"{}\" ignored", key));
        }
    }
    for (const std::string& warning : set.warnings)
    {
        INTERI_LOG_WARN("Audio", "{}", warning);
    }
    return set;
}

Result<DestructionAudioConfigSet> LoadDestructionAudioConfig(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return MakeError(ErrorCode::IoError, std::format("cannot read '{}'", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return ParseDestructionAudioConfig(text.str());
}

std::optional<std::uint32_t> SelectImpactBank(std::span<const ImpactBank> banks, StringId a, StringId b)
{
    std::optional<std::uint32_t> best;
    int bestScore = -1;
    for (std::uint32_t i = 0; i < banks.size(); ++i)
    {
        const ImpactBank& bank = banks[i];
        const auto side = [](StringId pattern, StringId material) -> int
        {
            if (!pattern.IsValid())
            {
                return 0; // "*"
            }
            return pattern == material && material.IsValid() ? 1 : -1;
        };
        const auto score = [&](StringId first, StringId second)
        {
            const int x = side(bank.idA, first);
            const int y = side(bank.idB, second);
            return x < 0 || y < 0 ? -1 : x + y;
        };
        const int s = std::max(score(a, b), score(b, a));
        if (s > bestScore)
        {
            bestScore = s;
            best = i;
        }
    }
    return best;
}

float ImpactVolume(const ImpactBank& bank, float impulse) noexcept
{
    if (!(impulse >= bank.minImpulse) || !(impulse > 0.0f))
    {
        return 0.0f;
    }
    const float t = LogRamp(impulse, std::max(bank.minImpulse, 1e-6f), bank.fullImpulse);
    return bank.volume * (bank.minVolume + (1.0f - bank.minVolume) * t);
}

float RumbleVolume(const RumbleCue& cue, float mass) noexcept
{
    if (!(mass >= cue.minMass) || !(mass > 0.0f))
    {
        return 0.0f;
    }
    return cue.volume * LogRamp(mass, std::max(cue.minMass, 1e-6f), cue.fullMass);
}

} // namespace interi::destruction
