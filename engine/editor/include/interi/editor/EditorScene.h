// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/editor/Reflection.h>
#include <interi/gameplay/Level.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

/// Stable identity of a document entity. ECS handles change when a deleted entity is restored by undo; ids don't.
using EditorEntityId = std::uint64_t;

/// Document components. Every document entity has an EditorIdComponent and a Transform, plus exactly one of the
/// kind components below (what the entity becomes in the level file).
struct EditorIdComponent
{
    static constexpr const char* kComponentName = "interi.editor.Id";
    std::uint64_t id = 0;
};

/// A static box (level `staticBoxes`). The Transform's scale is not used.
struct StaticBoxComponent
{
    static constexpr const char* kComponentName = "interi.editor.StaticBox";
    Vec3 halfExtents{0.5f};
};

/// A destructible instance (level `destructibles`).
struct DestructiblePlacementComponent
{
    static constexpr const char* kComponentName = "interi.editor.Destructible";
    std::string asset; ///< Virtual path of a cooked `.idest`.
};

/// A spawn point (level `spawnPoints`). Its yaw is the Transform's rotation about +Y.
struct SpawnPointComponent
{
    static constexpr const char* kComponentName = "interi.editor.SpawnPoint";
    std::string name;
};

/// One reflected component's field values, in reflection order.
struct ComponentSnapshot
{
    std::string component;
    std::vector<FieldValue> values;
};

/// Every reflected component of a document entity, ordered by component name.
struct EntitySnapshot
{
    EditorEntityId id = 0;
    std::vector<ComponentSnapshot> components;
};

/// What kind of level entry an entity is.
enum class EntityKind : std::uint8_t
{
    None,
    StaticBox,
    Destructible,
    SpawnPoint,
};

/// The level being edited, as ECS entities with document components. Converts to and from LevelDef; entries keep
/// their order (by id within each list). Only reflected components are snapshotted and restored, so document
/// entities carry reflected components only.
class EditorScene
{
public:
    explicit EditorScene(const ReflectionRegistry& reflection);

    [[nodiscard]] World& GetWorld() noexcept { return m_world; }
    [[nodiscard]] const World& GetWorld() const noexcept { return m_world; }
    [[nodiscard]] const ReflectionRegistry& Reflection() const noexcept { return *m_reflection; }

    /// Replaces the scene with the level's entries: static boxes, then destructibles, then spawn points, with ids
    /// 1, 2, ... in that order.
    void Load(const gameplay::LevelDef& level);
    [[nodiscard]] gameplay::LevelDef ToLevel() const;

    /// Removes every entity (ids restart at 1).
    void Clear();

    EditorEntityId CreateStaticBox(const Transform& transform, const Vec3& halfExtents);
    EditorEntityId CreateDestructible(const Transform& transform, std::string asset);
    EditorEntityId CreateSpawnPoint(const Transform& transform, std::string name);

    bool Destroy(EditorEntityId id);
    [[nodiscard]] bool Contains(EditorEntityId id) const noexcept { return m_entities.contains(id); }
    [[nodiscard]] std::optional<Entity> Find(EditorEntityId id) const;
    /// Every id, ascending.
    [[nodiscard]] std::vector<EditorEntityId> Ids() const;
    [[nodiscard]] std::size_t Size() const noexcept { return m_entities.size(); }
    [[nodiscard]] EntityKind KindOf(EditorEntityId id) const;

    [[nodiscard]] std::optional<ComponentSnapshot> ReadComponent(EditorEntityId id, std::string_view component) const;
    /// Writes every field of an existing component. False if the entity lacks it or a value has the wrong type.
    bool WriteComponent(EditorEntityId id, const ComponentSnapshot& snapshot);

    [[nodiscard]] std::optional<EntitySnapshot> Snapshot(EditorEntityId id) const;
    /// Recreates a snapshotted entity under its id. False if that id is in use or a component is unknown.
    bool Restore(const EntitySnapshot& snapshot);

    /// The next id Create* will use.
    [[nodiscard]] EditorEntityId NextId() const noexcept { return m_nextId; }

private:
    EditorEntityId Create(const Transform& transform);

    const ReflectionRegistry* m_reflection;
    World m_world;
    std::map<EditorEntityId, Entity> m_entities;
    EditorEntityId m_nextId = 1;
};

/// Rotation about +Y by `yaw` radians (FollowCamera convention: 0 looks along -Z), and back. YawOf ignores tilt.
[[nodiscard]] Quat YawRotation(float yaw) noexcept;
[[nodiscard]] float YawOf(const Quat& rotation) noexcept;

} // namespace interi::editor
