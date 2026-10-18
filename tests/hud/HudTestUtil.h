// SPDX-License-Identifier: MIT
#pragma once

#include <interi/chain/Expression.h>
#include <interi/hud/HudDocument.h>
#include <interi/values/Value.h>

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <string_view>

namespace interi::hud::hudtest
{

/// Values by id, for laying out documents.
class MapValues final : public chain::IValueReader
{
public:
    [[nodiscard]] const values::Value* Read(std::string_view id) const override
    {
        const auto found = m_values.find(id);
        return found == m_values.end() ? nullptr : &found->second;
    }
    void Set(std::string id, values::Value value) { m_values.insert_or_assign(std::move(id), std::move(value)); }

private:
    std::map<std::string, values::Value, std::less<>> m_values;
};

inline HudDocument MustParse(std::string_view json)
{
    auto parsed = ParseHud(json);
    if (!parsed)
    {
        ADD_FAILURE() << parsed.error().message;
        return {};
    }
    return std::move(parsed->document);
}

} // namespace interi::hud::hudtest
