// SPDX-License-Identifier: MIT
#pragma once

// What every importer shares: a mesh as per-corner attributes (a triangle soup) becomes a welded TriangleMesh with
// the settings applied. Private to the library.

#include <interi/asset_import/ModelImport.h>
#include <interi/core/math/Mat4.h>

#include <string>
#include <vector>

namespace interi::asset_import
{

/// Three corners per triangle, as the file gave them. Optional attributes are empty or one per corner.
struct CornerMesh
{
    std::string name;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<scene::TexCoord> uvs;
    std::vector<Vec4> tangents;
    std::vector<assets::SkinInfluence> skin;
    std::vector<std::int32_t> materials; ///< One per triangle; -1 for none.
    std::int32_t skeleton = -1;
};

/// The matrix taking the source's coordinates to engine space: the unit `scale`, then handedness, then the up axis.
[[nodiscard]] Mat4 SourceToEngine(const ModelImportSettings& settings);

/// Applies `transform` (placement) to the corners, welds positions, drops degenerate triangles, generates normals and
/// tangents, assigns slots and optimizes, per `settings`. Degenerate transforms and non-finite data are errors.
[[nodiscard]] Result<ImportedMesh> BuildMesh(CornerMesh corners, const Mat4& transform,
                                             const ModelImportSettings& settings, std::vector<std::string>& warnings);

/// Rotation, translation and scale of an affine matrix (negative determinant: the x scale is negative).
[[nodiscard]] assets::JointTransform Decompose(const Mat4& matrix);

/// Takes a skeleton from the source's space into engine space: joint world matrices W become C W C^-1 (so a skin
/// matrix keeps working on converted vertices), then locals are rebuilt from them.
void ConvertSkeleton(assets::Skeleton& skeleton, const Mat4& conversion);
/// Converts the keys of a clip the same way (translations by C, rotations by conjugation, scales permuted).
void ConvertClip(assets::AnimationClip& clip, const Mat4& conversion);

} // namespace interi::asset_import
