// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/editor/Reflection.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/Primitives.h>
#include <interi/scene/SceneComponents.h>
#include <interi/scene/SceneFile.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// Stable identity of a document object: its scene id (scene::StableIdComponent), which chains also use. ECS handles
/// change when a deleted object is restored by undo; ids don't.
using EditorEntityId = scene::ObjectId;

/// One reflected component's field values, in reflection order.
struct ComponentSnapshot
{
    std::string component;
    std::vector<FieldValue> values;
};

/// Every reflected component of a document object, ordered by component name, plus the components no loaded module
/// reflects (kept as written).
struct EntitySnapshot
{
    EditorEntityId id = 0;
    std::vector<ComponentSnapshot> components;
    std::vector<scene::SceneComponent> kept;
};

/// The scene being edited: generic objects (scene/SceneComponents.h: a stable id, name, parent and local transform,
/// plus any reflected components) as ECS entities. World transforms are kept up to date after every change. Only
/// reflected components are snapshotted and restored, so objects carry reflected components only (and kept ones).
class EditorScene
{
public:
    explicit EditorScene(const ReflectionRegistry& reflection);

    [[nodiscard]] World& GetWorld() noexcept { return m_world; }
    [[nodiscard]] const World& GetWorld() const noexcept { return m_world; }
    [[nodiscard]] const ReflectionRegistry& Reflection() const noexcept { return *m_reflection; }

    /// Replaces the scene with a document's objects (all or nothing). Warnings (unknown components, broken parents)
    /// are returned.
    [[nodiscard]] Result<std::vector<std::string>> Load(const scene::SceneDocument& document);
    [[nodiscard]] scene::SceneDocument ToDocument() const;

    /// Removes every object (ids restart at 1).
    void Clear();

    /// A new object with the standard components (a name, the parent, `local` relative to it).
    EditorEntityId CreateObject(std::string name, const Transform& local = {}, EditorEntityId parent = 0);
    /// A new object with a PrimitiveComponent (named after the shape when `name` is empty).
    EditorEntityId CreatePrimitive(const scene::PrimitiveParams& params, const Transform& local = {},
                                   std::string name = {}, EditorEntityId parent = 0);

    /// Destroys one object (its children become roots until they are destroyed or reparented; delete subtrees
    /// children first).
    bool Destroy(EditorEntityId id);
    [[nodiscard]] bool Contains(EditorEntityId id) const noexcept { return m_entities.contains(id); }
    [[nodiscard]] std::optional<Entity> Find(EditorEntityId id) const;
    /// Every id, ascending.
    [[nodiscard]] std::vector<EditorEntityId> Ids() const;
    [[nodiscard]] std::size_t Size() const noexcept { return m_entities.size(); }

    [[nodiscard]] const scene::HierarchyIndex& Hierarchy() const noexcept { return m_hierarchy; }
    [[nodiscard]] std::string_view NameOf(EditorEntityId id) const;
    [[nodiscard]] EditorEntityId ParentOf(EditorEntityId id) const;
    /// The object and its descendants, parents first.
    [[nodiscard]] std::vector<EditorEntityId> Subtree(EditorEntityId id) const { return m_hierarchy.Subtree(id); }
    [[nodiscard]] Transform WorldTransform(EditorEntityId id) const;
    [[nodiscard]] Transform LocalTransform(EditorEntityId id) const;

    [[nodiscard]] std::optional<ComponentSnapshot> ReadComponent(EditorEntityId id, std::string_view component) const;
    /// Writes every field of an existing component. False if the object lacks it or a value has the wrong type.
    bool WriteComponent(EditorEntityId id, const ComponentSnapshot& snapshot);
    /// Adds a reflected component with its default values. False if unknown or already present.
    bool AddComponent(EditorEntityId id, std::string_view component);
    /// Removes a reflected component (not one of the standard ones). False if absent.
    bool RemoveComponent(EditorEntityId id, std::string_view component);

    [[nodiscard]] std::optional<EntitySnapshot> Snapshot(EditorEntityId id) const;
    /// Recreates a snapshotted object under its id. False if that id is in use or a component is unknown.
    bool Restore(const EntitySnapshot& snapshot);

    /// The next id CreateObject will use.
    [[nodiscard]] EditorEntityId NextId() const noexcept { return m_nextId; }

    /// Recomputes world transforms and the hierarchy (done after every change made through this class; call it after
    /// writing the world directly).
    void Refresh();

private:
    const ReflectionRegistry* m_reflection;
    mutable World m_world; ///< Queries need a mutable world; reading never changes what the document holds.
    std::map<EditorEntityId, Entity> m_entities;
    scene::KeptComponents m_kept;
    scene::HierarchyIndex m_hierarchy;
    EditorEntityId m_nextId = 1;
};

/// Rotation about +Y by `yaw` radians (FollowCamera convention: 0 looks along -Z), and back. YawOf ignores tilt.
[[nodiscard]] Quat YawRotation(float yaw) noexcept;
[[nodiscard]] float YawOf(const Quat& rotation) noexcept;

} // namespace interi::editor
