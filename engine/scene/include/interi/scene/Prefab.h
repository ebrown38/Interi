// SPDX-License-Identifier: MIT
#pragma once

// Prefabs (.prefab.json): a scene v2 document with exactly one root, instanced into scenes.
//
// In a saved scene an instance is one object, its root: its own name, parent and local transform, plus a
// PrefabInstanceComponent naming the prefab and listing the instance's overrides. Loading expands it (ExpandPrefabs):
// the root takes the prefab root's components, every other prefab object becomes an object with the derived id
// PrefabObjectId(root, source), parent links follow, every expanded object gets a PrefabLinkComponent, and the
// overrides are applied last. Saving collapses it again (CollapsePrefabs): every field of an expanded object that
// differs from the prefab becomes an override ("<source>/<component>/<field>=<json>"), and the expanded objects are
// dropped. So editing a prefab changes every instance, except where an instance overrides it.
//
// Limits: entity references other than parent links are not remapped; deleting an expanded object or removing a
// component from it is not recorded (it comes back on load). Prefabs don't nest (an instance inside a prefab is
// kept as a plain object).

#include <interi/chain/Executor.h>
#include <interi/core/Result.h>
#include <interi/ecs/World.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/SceneFile.h>
#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::scene
{

/// On an instance root.
struct PrefabInstanceComponent
{
    static constexpr const char* kComponentName = "interi.PrefabInstance";
    values::AssetRef prefab;
    values::ValueList overrides{.element = values::ValueType::String, .items = {}};
};

/// On every object an instance expanded into (its root too).
struct PrefabLinkComponent
{
    static constexpr const char* kComponentName = "interi.PrefabLink";
    values::EntityRef instance; ///< The instance root.
    values::EntityRef source;   ///< The object's id in the prefab.
};

void RegisterPrefabReflection(values::ReflectionRegistry& registry);

/// Prefab objects have ids below this (and instance roots below 2^47).
inline constexpr ObjectId kMaxPrefabObjects = ObjectId{1} << 16;

/// The id of the object instance `root` expands prefab object `source` into (not the root itself, which keeps its id).
[[nodiscard]] constexpr ObjectId PrefabObjectId(ObjectId root, ObjectId source) noexcept
{
    return (ObjectId{1} << 63) | (root << 16) | source;
}
[[nodiscard]] constexpr bool IsPrefabObjectId(ObjectId id) noexcept
{
    return (id >> 63) != 0;
}

struct PrefabOverride
{
    ObjectId source = 0;
    std::string component;
    std::string field;
    std::string json;

    friend bool operator==(const PrefabOverride&, const PrefabOverride&) = default;
};

[[nodiscard]] std::string FormatOverride(const PrefabOverride& entry);
/// InvalidArgument for text that isn't "<source>/<component>/<field>=<json>".
[[nodiscard]] Result<PrefabOverride> ParseOverride(std::string_view text);

/// The prefab's root id. InvalidArgument unless exactly one object has no parent, every parent exists and every id
/// is below kMaxPrefabObjects.
[[nodiscard]] Result<ObjectId> PrefabRoot(const SceneDocument& prefab);

/// Reads a prefab by virtual path.
using PrefabLoader = std::function<Result<SceneDocument>(std::string_view path)>;

/// Caches prefabs read from a file system (checked with PrefabRoot). Not thread-safe.
class PrefabLibrary
{
public:
    explicit PrefabLibrary(const IFileSystem& files)
        : m_files(&files)
    {
    }

    [[nodiscard]] Result<SceneDocument> Load(std::string_view path);
    [[nodiscard]] PrefabLoader Loader()
    {
        return [this](std::string_view path) { return Load(path); };
    }
    /// Forgets a cached prefab (after it is rewritten), or all of them.
    void Forget(std::string_view path) { m_cache.erase(std::string(path)); }
    void Clear() noexcept { m_cache.clear(); }

private:
    const IFileSystem* m_files;
    std::map<std::string, SceneDocument, std::less<>> m_cache;
};

/// The objects of one instance: `root` (its name, parent, local transform and PrefabInstance) expanded with its
/// prefab, overrides applied. Warnings name overrides that no longer fit (an unknown source object). Fails if the
/// prefab can't be loaded or isn't a valid prefab.
[[nodiscard]] Result<std::vector<SceneObject>> ExpandInstance(const SceneObject& root, const PrefabLoader& load,
                                                              std::vector<std::string>& warnings);

/// Expands every instance root in a document (replacing any expanded objects it already holds). An instance whose
/// prefab can't be loaded stays a lone root, with a warning.
void ExpandPrefabs(SceneDocument& document, const PrefabLoader& load, std::vector<std::string>& warnings);

/// The inverse: records each instance's differences from its prefab as overrides on its root and drops the
/// expanded objects (objects added under them are kept). Instances whose prefab can't be loaded are left expanded.
/// The prefab's objects are first resolved through `reflection` (fields a prefab file leaves out take their
/// defaults, numbers their canonical text), so only real differences become overrides.
void CollapsePrefabs(SceneDocument& document, const PrefabLoader& load, const values::ReflectionRegistry& reflection);

/// A prefab made of `root` and its descendants in `document`: ids renumbered from 1 (the root) in id order, the
/// root without a parent and at the origin, prefab components dropped. InvalidArgument if `root` is missing or the
/// subtree has kMaxPrefabObjects objects or more.
[[nodiscard]] Result<SceneDocument> MakePrefab(const SceneDocument& document, ObjectId root);

/// Spawns the prefabs chains asked for (ChainExecutor::Spawns, after its Step): each root takes the request's id
/// and transform, the rest of the prefab follows. Returns the roots spawned, in request order (raise
/// trigger.Spawned for them); a request whose prefab can't be loaded or instantiated is skipped with a warning.
std::vector<ObjectId> SpawnPrefabs(std::span<const chain::SpawnRequest> requests, World& world,
                                   const values::ReflectionRegistry& reflection, const PrefabLoader& load,
                                   std::vector<std::string>& warnings);

/// A field's JSON in canonical form (so values written by different code compare equal).
[[nodiscard]] std::string CanonicalJson(std::string_view json);

} // namespace interi::scene
