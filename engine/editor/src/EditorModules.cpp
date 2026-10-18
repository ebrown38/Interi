// SPDX-License-Identifier: MIT
#include <interi/editor/EditorModules.h>

namespace interi::editor
{

Result<std::unique_ptr<EditorModules>> EditorModules::Open(const sdk::Project& project,
                                                           const game::ProjectModulesDesc& desc)
{
    std::unique_ptr<EditorModules> modules(new EditorModules());
    INTERI_TRY_ASSIGN(opened, game::ProjectModules::Open(project, modules->m_world, desc));
    modules->m_modules = std::move(opened);
    return modules;
}

} // namespace interi::editor
