// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelImport.h>

#include "ImportUtil.h"

#include <cmath>
#include <format>

namespace interi::asset_import
{

Result<void> ValidateSettings(const ModelImportSettings& settings)
{
    if (!std::isfinite(settings.scale) || settings.scale <= 0.0f)
    {
        return MakeError(ErrorCode::InvalidArgument, "import settings: scale must be positive and finite");
    }
    if (!std::isfinite(settings.smoothingAngleDegrees) || settings.smoothingAngleDegrees < 0.0f ||
        settings.smoothingAngleDegrees > 180.0f)
    {
        return MakeError(ErrorCode::InvalidArgument, "import settings: smoothingAngleDegrees must be in [0, 180]");
    }
    return {};
}

bool IsModelPath(std::string_view path)
{
    const std::string extension = ExtensionOf(path);
    return extension == ".gltf" || extension == ".glb" || extension == ".obj" || extension == ".fbx";
}

Result<ImportedModel> ImportModel(const IFileSystem& files, std::string_view path, const ModelImportSettings& settings)
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    const std::string extension = ExtensionOf(normalized);
    if (!IsModelPath(normalized))
    {
        return MakeError(ErrorCode::Unsupported,
                         std::format("{}: not a model file (.gltf, .glb, .obj, .fbx)", normalized));
    }
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(normalized));
    const std::string directory = DirectoryOf(normalized);
    Result<ImportedModel> model = MakeError(ErrorCode::Unsupported, "unreachable");
    if (extension == ".gltf" || extension == ".glb")
    {
        model = ImportGltfData(bytes, files, directory, settings);
    }
    else if (extension == ".obj")
    {
        model = ImportObjData(bytes, files, directory, settings);
    }
    else
    {
        model = ImportFbxData(bytes, directory, settings);
    }
    if (!model)
    {
        return MakeError(model.error().code, std::format("{}: {}", normalized, model.error().message));
    }
    model->name = StemOf(normalized);
    return model;
}

} // namespace interi::asset_import
