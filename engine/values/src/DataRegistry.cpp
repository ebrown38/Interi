// SPDX-License-Identifier: MIT
#include <interi/values/DataRegistry.h>

namespace interi::values
{

DataRegistry::DataRegistry(const sdk::ModuleSet& modules)
    : m_values(modules)
    , m_characters(modules)
{
}

Result<void> DataRegistry::AddModule(const ModuleContent& content)
{
    INTERI_TRY(m_values.AddModule(content));
    if (Result<void> characters = m_characters.AddModule(content); !characters)
    {
        (void)m_values.ExtractProvider(content.module);
        return characters;
    }
    return {};
}

ExtractedData DataRegistry::ExtractProvider(std::string_view provider)
{
    ExtractedData data;
    data.values = m_values.ExtractProvider(provider);
    data.characters = m_characters.ExtractProvider(provider);
    return data;
}

void DataRegistry::Restore(ExtractedData data)
{
    m_values.Restore(std::move(data.values));
    m_characters.Restore(std::move(data.characters));
}

std::vector<std::string> DataRegistry::Revalidate()
{
    std::vector<std::string> warnings = m_values.Revalidate();
    std::vector<std::string> characters = m_characters.Revalidate();
    warnings.insert(warnings.end(), characters.begin(), characters.end());
    return warnings;
}

} // namespace interi::values
