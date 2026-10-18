// SPDX-License-Identifier: MIT
#include <interi/values/DataRegistry.h>

namespace interi::values
{

DataRegistry::DataRegistry(const sdk::ModuleSet& modules)
    : m_values(modules)
    , m_characters(modules)
    , m_tables(modules)
{
}

Result<void> DataRegistry::AddModule(const ModuleContent& content)
{
    INTERI_TRY(m_values.AddModule(content));
    Result<void> added = m_characters.AddModule(content);
    if (added)
    {
        added = m_tables.AddTables(content.module, content.dataTables);
    }
    if (!added)
    {
        (void)ExtractProvider(content.module);
    }
    return added;
}

ExtractedData DataRegistry::ExtractProvider(std::string_view provider)
{
    ExtractedData data;
    data.values = m_values.ExtractProvider(provider);
    data.characters = m_characters.ExtractProvider(provider);
    data.tables = m_tables.ExtractProvider(provider);
    return data;
}

void DataRegistry::Restore(ExtractedData data)
{
    m_values.Restore(std::move(data.values));
    m_characters.Restore(std::move(data.characters));
    m_tables.Restore(std::move(data.tables));
}

std::vector<std::string> DataRegistry::Revalidate()
{
    std::vector<std::string> warnings = m_values.Revalidate();
    std::vector<std::string> characters = m_characters.Revalidate();
    warnings.insert(warnings.end(), characters.begin(), characters.end());
    for (const TableIssue& issue : m_tables.Validate())
    {
        warnings.push_back(issue.Text() + "; using the default");
    }
    return warnings;
}

void DataRegistry::ClearProject()
{
    m_values.ClearProjectValues();
    m_characters.ClearProjectDefaults();
    m_tables.ClearRows();
    m_unresolved = {};
    m_loadedVersions.clear();
}

} // namespace interi::values
