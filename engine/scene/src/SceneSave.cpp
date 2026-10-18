// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/core/Log.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneSave.h>

#include <algorithm>
#include <map>

namespace interi::scene
{

namespace
{

constexpr FourCC kStateMagic = MakeFourCC("ISCN");

struct ObjectState
{
    std::uint64_t id = 0;
    Vec3 position;
    Quat rotation;
    Vec3 scale{1.0f};
    bool hidden = false;
};

struct OverrideState
{
    std::string name;
    values::Value value;
};

struct SceneState
{
    std::vector<OverrideState> overrides;
    std::vector<ObjectState> objects;
};

template <typename Archive>
void Serialize(Archive& archive, ObjectState& object)
{
    archive(object.id, object.position, object.rotation.x, object.rotation.y, object.rotation.z, object.rotation.w,
            object.scale, object.hidden);
}

template <typename Archive>
void Serialize(Archive& archive, OverrideState& entry)
{
    archive(entry.name, entry.value);
}

template <typename Archive>
void Serialize(Archive& archive, SceneState& state)
{
    archive(state.overrides, state.objects);
}

} // namespace

std::vector<std::byte> CaptureSceneState(SceneRuntime& runtime)
{
    SceneState state;
    for (const auto& [id, value] : runtime.Executor().Values().Overrides())
    {
        state.overrides.push_back({.name = id, .value = value});
    }
    World& world = runtime.GetWorld();
    for (const auto& [id, node] : BuildHierarchy(world).nodes)
    {
        const auto* local = world.Get<LocalTransformComponent>(node.entity);
        if (local == nullptr)
        {
            continue;
        }
        ObjectState object;
        object.id = id;
        object.position = local->position;
        object.rotation = local->rotation;
        object.scale = local->scale;
        object.hidden = world.Has<chain::HiddenComponent>(node.entity);
        state.objects.push_back(object);
    }
    std::vector<std::byte> bytes;
    ArchiveWriter writer(bytes, kStateMagic, kSceneSaveVersion);
    writer(state);
    return bytes;
}

Result<void> RestoreSceneState(SceneRuntime& runtime, std::span<const std::byte> data, std::uint32_t savedVersion)
{
    (void)savedVersion; // one version so far
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(data, kStateMagic, kSceneSaveVersion));
    SceneState state;
    reader(state);
    INTERI_TRY(reader.Finish());

    World& world = runtime.GetWorld();
    const HierarchyIndex index = BuildHierarchy(world);
    std::map<std::uint64_t, const ObjectState*> saved;
    for (const ObjectState& object : state.objects)
    {
        saved.emplace(object.id, &object);
    }
    // Objects the save doesn't know are gone; the rest take the saved placement.
    for (const auto& [id, node] : index.nodes)
    {
        const auto found = saved.find(id);
        if (found == saved.end())
        {
            world.DestroyEntity(node.entity);
            continue;
        }
        const ObjectState& object = *found->second;
        if (auto* local = world.GetMut<LocalTransformComponent>(node.entity))
        {
            local->position = object.position;
            local->rotation = object.rotation;
            local->scale = object.scale;
        }
        const bool hidden = world.Has<chain::HiddenComponent>(node.entity);
        if (object.hidden && !hidden)
        {
            world.Add(node.entity, chain::HiddenComponent{});
        }
        else if (!object.hidden && hidden)
        {
            world.Remove<chain::HiddenComponent>(node.entity);
        }
    }
    for (const auto& [id, object] : saved)
    {
        if (!index.nodes.contains(id))
        {
            INTERI_LOG_WARN("Scene", "save lists object {} but the scene has no such object", id);
        }
    }
    (void)PropagateTransforms(world);

    runtime.Executor().Values().Clear();
    for (OverrideState& entry : state.overrides)
    {
        if (auto set = runtime.Executor().Values().Set(entry.name, std::move(entry.value)); !set)
        {
            INTERI_LOG_WARN("Scene", "save value '{}' dropped: {}", entry.name, set.error().message);
        }
    }
    return {};
}

Result<void> RegisterSceneSaveSection(SaveRegistry& registry, SceneRuntime& runtime)
{
    return registry.Add(kEngineSaveProvider,
                        {.id = std::string(kSceneSaveSection),
                         .version = kSceneSaveVersion,
                         .capture = [&runtime] { return CaptureSceneState(runtime); },
                         .restore = [&runtime](std::span<const std::byte> data, std::uint32_t version)
                         { return RestoreSceneState(runtime, data, version); }});
}

} // namespace interi::scene
