// SPDX-License-Identifier: MIT
#pragma once

// Small previews of assets for the asset browser, rendered on the CPU: textures scaled to fit, meshes and models shaded
// from a fixed viewpoint, materials as the preview sphere. Scenes, prefabs, skeletons and clips have none.

#include <interi/assets/Texture.h>
#include <interi/core/Result.h>
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/MaterialPreview.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace interi::editor
{

/// Square RGBA8 image; alpha 0 where nothing is drawn.
struct Thumbnail
{
    std::uint32_t size = 0;
    std::vector<std::uint8_t> rgba;
};

/// `texture` scaled to fit `size` x `size`, aspect ratio kept (nearest texel of the closest mip).
[[nodiscard]] Thumbnail TextureThumbnail(const assets::TextureAsset& texture, std::uint32_t size);
/// `mesh` lit from the upper left and seen from a fixed direction, framed by its bounds.
[[nodiscard]] Thumbnail MeshThumbnail(const scene::TriangleMesh& mesh, std::uint32_t size);

/// A sampler for RenderMaterialPreview that reads cooked textures through `files` (each once). A material that points
/// at a source file (`tex/a.png`, as authored) is looked up as `<importedFolder>/tex/a.png.itex`. Unreadable textures
/// sample as white. The sampler keeps `files` by reference.
[[nodiscard]] TextureSampler MakeTextureSampler(const IFileSystem& files, std::string importedFolder = "imported");

/// Reads the asset and renders it. Unsupported for kinds without a thumbnail; the file's errors otherwise.
[[nodiscard]] Result<Thumbnail> MakeThumbnail(const IFileSystem& files, const AssetEntry& entry, std::uint32_t size);

/// Thumbnails by path, made on first use. A failure is remembered too (so a broken file isn't re-read every frame)
/// until Invalidate or Clear.
class ThumbnailCache
{
public:
    explicit ThumbnailCache(std::uint32_t size = 32)
        : m_size(size)
    {
    }

    /// The thumbnail, or nullptr if the asset has none.
    [[nodiscard]] const Thumbnail* Get(const IFileSystem& files, const AssetEntry& entry);
    void Invalidate(const std::string& path) { m_entries.erase(path); }
    void Clear() { m_entries.clear(); }
    [[nodiscard]] std::size_t Count() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::uint32_t Size() const noexcept { return m_size; }

private:
    std::uint32_t m_size;
    std::map<std::string, std::optional<Thumbnail>> m_entries;
};

} // namespace interi::editor
