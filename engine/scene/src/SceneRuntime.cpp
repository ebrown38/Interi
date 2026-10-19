// SPDX-License-Identifier: MIT
#include <interi/chain/ChainAsset.h>
#include <interi/chain/CoreLibrary.h>
#include <interi/ecs/Query.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Prefab.h>
#include <interi/scene/Primitives.h>
#include <interi/scene/SceneRuntime.h>
#include <interi/scene/Triggers.h>

#include <algorithm>
#include <format>
#include <map>

namespace interi::scene
{

void RegisterChainsReflection(values::ReflectionRegistry& registry)
{
    registry.Register<ChainsComponent>({
        values::Field("chains", &ChainsComponent::chains),
        values::Field("global", &ChainsComponent::global),
    });
}

void RegisterSceneRuntimeReflection(values::ReflectionRegistry& registry)
{
    RegisterSceneReflection(registry);
    RegisterPrimitiveReflection(registry);
    RegisterMeshReflection(registry);
    RegisterPrefabReflection(registry);
    RegisterChainsReflection(registry);
    RegisterTriggerReflection(registry);
}

SceneRuntime::SceneRuntime(const values::ReflectionRegistry& reflection, chain::ChainRegistry& chains,
                           values::ValueRegistry* values, const IFileSystem& files, JobSystem& jobs,
                           const SceneRuntimeDesc& desc)
    : m_reflection(&reflection)
    , m_jobs(&jobs)
    , m_world(desc.world != nullptr ? *desc.world : m_ownedWorld)
    , m_commands(jobs)
    , m_executor(chains, values, desc.executor)
    , m_prefabs(files)
{
}

SceneRuntime::~SceneRuntime() = default;

Result<std::unique_ptr<SceneRuntime>> SceneRuntime::Create(SceneDocument document,
                                                           const values::ReflectionRegistry& reflection,
                                                           chain::ChainRegistry& chains, values::ValueRegistry* values,
                                                           const IFileSystem& files, JobSystem& jobs,
                                                           const SceneRuntimeDesc& desc)
{
    std::unique_ptr<SceneRuntime> runtime(new SceneRuntime(reflection, chains, values, files, jobs, desc));
    ExpandPrefabs(document, runtime->m_prefabs.Loader(), runtime->m_warnings);
    INTERI_TRY_ASSIGN(instantiated, InstantiateScene(document, runtime->m_world, reflection));
    runtime->m_warnings.insert(runtime->m_warnings.end(), instantiated.warnings.begin(), instantiated.warnings.end());

    // Chains in object id order, each object's in list order (the executor orders activations by priority, entity
    // and sequence, so this order only matters for ties).
    for (const auto& [id, entity] : instantiated.entities)
    {
        const auto* component = runtime->m_world.Get<ChainsComponent>(entity);
        if (component == nullptr)
        {
            continue;
        }
        for (const values::Value& item : component->chains.items)
        {
            const auto* path = std::get_if<values::AssetRef>(&item.Variant());
            if (path == nullptr || path->path.empty())
            {
                continue;
            }
            auto asset = chain::LoadChain(files, path->path);
            if (!asset)
            {
                runtime->m_warnings.push_back(std::format("object {}: {}", id, asset.error().message));
                continue;
            }
            const chain::ChainExecutor::ChainId chain =
                runtime->m_executor.AddChain(std::move(*asset), component->global ? 0 : id);
            runtime->m_chainPaths.emplace(chain, path->path);
            if (!runtime->m_executor.IsActive(chain))
            {
                for (const chain::Diagnostic& diagnostic : runtime->m_executor.Diagnostics(chain))
                {
                    runtime->m_warnings.push_back(std::format("{}: {}", path->path, diagnostic.Text()));
                }
            }
        }
    }
    return runtime;
}

std::string_view SceneRuntime::ChainPath(chain::ChainExecutor::ChainId chain) const noexcept
{
    const auto found = m_chainPaths.find(chain);
    return found == m_chainPaths.end() ? std::string_view{} : std::string_view(found->second);
}

void SceneRuntime::Step()
{
    // World transforms as propagated, to see which ones the chains move.
    const HierarchyIndex before = BuildHierarchy(m_world);
    std::map<ObjectId, Transform> worlds;
    for (const auto& [id, node] : before.nodes)
    {
        if (const auto* transform = m_world.Get<Transform>(node.entity))
        {
            worlds.emplace(id, *transform);
        }
    }

    m_executor.Step(m_world, *m_jobs, m_commands);

    for (const ObjectId id : before.PreOrder())
    {
        const HierarchyIndex::Node& node = before.nodes.at(id);
        if (!m_world.IsAlive(node.entity))
        {
            continue;
        }
        const auto* transform = m_world.Get<Transform>(node.entity);
        auto* local = m_world.GetMut<LocalTransformComponent>(node.entity);
        const auto previous = worlds.find(id);
        if (transform == nullptr || local == nullptr || previous == worlds.end() || *transform == previous->second)
        {
            continue;
        }
        const auto parent = worlds.find(node.parent);
        const Transform adopted = parent == worlds.end() ? *transform : parent->second.Inverse() * *transform;
        *local = LocalTransformComponent::From(adopted);
    }
    (void)PropagateTransforms(m_world);

    if (!m_executor.Spawns().empty())
    {
        for (const ObjectId root :
             SpawnPrefabs(m_executor.Spawns(), m_world, *m_reflection, m_prefabs.Loader(), m_warnings))
        {
            m_executor.RaiseSpawned(root);
        }
    }

    // Overlaps, from the final transforms. Raised for the next step.
    const TriggerScan scan = ScanTriggers(m_world);
    for (const OverlapEvent& event : m_overlaps.Update(scan.volumes, scan.actors))
    {
        const auto volume = std::ranges::lower_bound(scan.volumes, event.volume, {}, &TriggerVolume::id);
        std::string tag;
        if (volume != scan.volumes.end() && volume->id == event.volume)
        {
            tag = scan.tags[static_cast<std::size_t>(volume - scan.volumes.begin())];
        }
        m_executor.RaiseTrigger(event.begin ? chain::kTriggerOverlap : chain::kTriggerOverlapEnd, event.volume,
                                std::move(tag), {values::EntityRef{.id = event.actor}});
    }
}

} // namespace interi::scene
