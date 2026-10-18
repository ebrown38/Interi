// SPDX-License-Identifier: MIT
#pragma once

// Scene v2 (.scene.json): objects keyed by stable id, each a set of components keyed by registry name
// (kComponentName), each a set of fields as JSON values. Keys are written sorted and objects by id, so files diff
// well.
//
//     {
//       "version": 2,
//       "objects": [
//         { "id": 1, "components": {
//             "interi.Name": { "name": "Floor" },
//             "interi.Parent": { "parent": 0 },
//             "interi.LocalTransform": { "position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
//             "interi.Primitive": { "shape": "Box", "size": [10, 0.5, 10] } } }
//       ]
//     }
//
// Field values use the values module's JSON (ValueToJsonText). The document keeps field text as written, so
// components that no loaded module reflects survive a load and save unchanged.

#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/SceneComponents.h>
#include <interi/values/Reflection.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi::scene
{

inline constexpr std::uint32_t kSceneFormatVersion = 2;

struct SceneComponent
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> fields; ///< Field name -> JSON text, sorted by name.

    friend bool operator==(const SceneComponent&, const SceneComponent&) = default;
};

struct SceneObject
{
    ObjectId id = 0;
    std::vector<SceneComponent> components; ///< Sorted by name.

    [[nodiscard]] const SceneComponent* Find(std::string_view component) const noexcept;

    friend bool operator==(const SceneObject&, const SceneObject&) = default;
};

struct SceneDocument
{
    std::vector<SceneObject> objects; ///< Ascending ids, unique, non-zero.
    std::vector<std::string> warnings;

    [[nodiscard]] const SceneObject* Find(ObjectId id) const noexcept;

    friend bool operator==(const SceneDocument& a, const SceneDocument& b) { return a.objects == b.objects; }
};

/// Parses .scene.json text. InvalidArgument (naming the object, component or field) for malformed content,
/// VersionMismatch for a newer version. Objects are sorted by id; unknown top-level keys are warnings.
[[nodiscard]] Result<SceneDocument> ParseScene(std::string_view json);
[[nodiscard]] Result<SceneDocument> LoadScene(const IFileSystem& files, std::string_view path);
/// Sorted keys, two-space indent; ParseScene reads it back equal.
[[nodiscard]] std::string WriteScene(const SceneDocument& document);

/// Components of scene objects that the reflection registry doesn't know, by object id (kept to be written back).
using KeptComponents = std::map<ObjectId, std::vector<SceneComponent>>;

struct InstantiatedScene
{
    std::map<ObjectId, Entity> entities;
    KeptComponents kept;
    std::vector<std::string> warnings; ///< Unknown components and fields, broken parent links.
};

/// Creates the document's objects in `world` (which must not already use their ids), then propagates transforms.
/// Missing standard components are added with defaults; fields a component lacks take the component's defaults;
/// unknown fields are dropped with a warning. All or nothing: InvalidArgument if a field's JSON doesn't fit its
/// type, or an id is already in the world.
[[nodiscard]] Result<InstantiatedScene> InstantiateScene(const SceneDocument& document, World& world,
                                                         const values::ReflectionRegistry& reflection);

/// The scene objects of `world` (entities with a StableIdComponent) as a document: every reflected component except
/// the world Transform, plus `kept` components for objects that still exist.
[[nodiscard]] SceneDocument CaptureScene(World& world, const values::ReflectionRegistry& reflection,
                                         const KeptComponents* kept = nullptr);

/// One entity as a scene object (nullopt if it has no StableIdComponent).
[[nodiscard]] std::optional<SceneObject> CaptureObject(World& world, Entity entity,
                                                       const values::ReflectionRegistry& reflection);

} // namespace interi::scene
