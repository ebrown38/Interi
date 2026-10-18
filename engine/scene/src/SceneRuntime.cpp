// SPDX-License-Identifier: MIT
#include <interi/chain/ChainAsset.h>
#include <interi/ecs/Query.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneRuntime.h>

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

SceneRuntime::SceneRuntime(const values::ReflectionRegistry& reflection, chain::ChainRegistry& chains,
                           values::ValueRegistry* values, const IFileSystem& files, JobSystem& jobs,
                           const SceneRuntimeDesc& desc)
    : m_reflection(&reflection)
    , m_jobs(&jobs)
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
}

} // namespace interi::scene
