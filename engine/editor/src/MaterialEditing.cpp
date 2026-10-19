// SPDX-License-Identifier: MIT
#include <interi/editor/MaterialEditing.h>

#include <format>
#include <memory>

namespace interi::editor
{

namespace
{

class MaterialSnapshotCommand final : public ICommand
{
public:
    MaterialSnapshotCommand(assets::PbrMaterial* target, assets::PbrMaterial after, std::string label,
                            std::string mergeKey)
        : m_target(target)
        , m_before(*target)
        , m_after(std::move(after))
        , m_label(std::move(label))
        , m_mergeKey(std::move(mergeKey))
    {
    }

    void Do() override { *m_target = m_after; }
    void Undo() override { *m_target = m_before; }
    [[nodiscard]] std::string_view Label() const override { return m_label; }

    bool MergeWith(const ICommand& next) override
    {
        const auto* other = dynamic_cast<const MaterialSnapshotCommand*>(&next);
        if (other == nullptr || m_mergeKey.empty() || other->m_mergeKey != m_mergeKey || other->m_target != m_target)
        {
            return false;
        }
        m_after = other->m_after;
        return true;
    }

private:
    assets::PbrMaterial* m_target;
    assets::PbrMaterial m_before;
    assets::PbrMaterial m_after;
    std::string m_label;
    std::string m_mergeKey;
};

} // namespace

Result<void> EditMaterial(UndoStack& undo, assets::PbrMaterial& material, assets::PbrMaterial edited,
                          std::string_view field)
{
    INTERI_TRY(assets::ValidateMaterial(edited));
    if (edited == material)
    {
        return {};
    }
    undo.Execute(std::make_unique<MaterialSnapshotCommand>(
        &material, std::move(edited), field.empty() ? std::string("Edit material") : std::format("Edit {}", field),
        std::string(field)));
    return {};
}

std::string TextureSourceOf(std::string_view assetPath, std::string_view importedFolder)
{
    const std::string prefix = std::string(importedFolder) + "/";
    if (assetPath.starts_with(prefix) && assetPath.ends_with(".itex") && assetPath.size() > prefix.size() + 5)
    {
        return std::string(assetPath.substr(prefix.size(), assetPath.size() - prefix.size() - 5));
    }
    return std::string(assetPath);
}

Result<assets::PbrMaterial> OpenMaterialFile(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto parsed = assets::ParseMaterial(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!parsed)
    {
        return MakeError(parsed.error().code, std::format("{}: {}", path, parsed.error().message));
    }
    return std::move(parsed->material);
}

Result<void> SaveMaterialFile(IFileSystem& files, std::string_view path, const assets::PbrMaterial& material)
{
    INTERI_TRY(assets::ValidateMaterial(material));
    const std::string text = assets::WriteMaterial(material);
    return files.WriteFile(path, {reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

} // namespace interi::editor
