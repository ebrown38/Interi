// SPDX-License-Identifier: MIT
#pragma once

// Simple modeling on the document's selection. Every operation is one undo step and acts on the selected roots
// (selected objects without a selected ancestor), in selection order.

#include <interi/core/Result.h>
#include <interi/core/math/Geometry.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Picking.h>
#include <interi/modeling/Csg.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace interi
{
class IFileSystem;
}

namespace interi::editor
{

/// An object's world-space bounding box (its LocalBounds' corners under its world transform), or just its position
/// for an object without a shape.
[[nodiscard]] AABB WorldBounds(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds);

/// Puts the selected roots under a new empty object at their bounds' centre (under their common parent, if they
/// share one) and selects it. Returns its id, or 0 with nothing selected.
EditorEntityId GroupSelection(EditorDocument& document, const AssetBoundsFn& bounds);

/// Moves the children of each selected object to its parent (keeping their world transforms) and deletes the
/// object if it has nothing but the standard components. False if no selected object has children.
bool UngroupSelection(EditorDocument& document);

/// Copies the selected roots with their subtrees (new ids; parent links and entity references inside a copied
/// subtree point at the copies), each copy `count` times, copy i moved by i x `offset` in its parent's space.
/// Selects the copies. Duplicate is count 1 with no offset.
std::vector<EditorEntityId> DuplicateSelection(EditorDocument& document, std::uint32_t count = 1,
                                               const Vec3& offset = {});

enum class AlignMode : std::uint8_t
{
    Min,
    Centre,
    Max,
};

/// Moves the selected roots along a world axis (0 = x, 1 = y, 2 = z) so their bounds' min, centre or max match
/// the selection's. False with fewer than two roots or nothing to move.
bool AlignSelection(EditorDocument& document, int axis, AlignMode mode, const AssetBoundsFn& bounds);

/// Spaces the selected roots' bounds centres evenly along a world axis between the outermost two (by centre, then
/// id). False with fewer than three roots or nothing to move.
bool DistributeSelection(EditorDocument& document, int axis, const AssetBoundsFn& bounds);

/// Mirrors the selected roots across the plane through the selection's bounds centre normal to a world axis: their
/// positions reflect and the local axis closest to the world axis has its scale negated.
bool MirrorSelection(EditorDocument& document, int axis, const AssetBoundsFn& bounds);

/// Snaps the selected roots' world positions to a grid (Snap). False if nothing moves.
bool SnapSelection(EditorDocument& document, float grid);

enum class PivotPlacement : std::uint8_t
{
    BoundsCentre,
    BoundsBottom, ///< The centre of the bounds' bottom face (stands objects on the ground).
};

/// Moves an object's origin to `world` without moving anything else: its own geometry offset (PrimitiveComponent /
/// MeshComponent) and its children's local transforms compensate. False if unchanged or the object is gone.
bool SetPivot(EditorDocument& document, EditorEntityId id, const Vec3& world);
bool SetPivot(EditorDocument& document, EditorEntityId id, PivotPlacement placement, const AssetBoundsFn& bounds);

/// The mesh an object draws: its primitive's, or its mesh asset's (read from `files`). NotFound without either.
[[nodiscard]] Result<scene::TriangleMesh> ObjectMesh(const EditorScene& scene, EditorEntityId id,
                                                     const IFileSystem& files);

/// Combines the selected roots' meshes (selection order: the first is the base) into a mesh asset written to
/// `meshPath`, replaces them with one object drawing it (at the result's bounds centre, under the first root's
/// parent) and selects it. The asset is written at once and not removed by undo.
[[nodiscard]] Result<EditorEntityId> CsgSelection(EditorDocument& document, IFileSystem& files,
                                                  modeling::CsgOperation operation, std::string meshPath);

} // namespace interi::editor
