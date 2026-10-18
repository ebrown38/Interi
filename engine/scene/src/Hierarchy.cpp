// SPDX-License-Identifier: MIT
#include <interi/ecs/Query.h>
#include <interi/scene/Hierarchy.h>

#include <algorithm>

namespace interi::scene
{

std::vector<ObjectId> HierarchyIndex::PreOrder() const
{
    std::vector<ObjectId> order;
    order.reserve(nodes.size());
    for (const ObjectId root : roots)
    {
        const std::vector<ObjectId> subtree = Subtree(root);
        order.insert(order.end(), subtree.begin(), subtree.end());
    }
    return order;
}

std::vector<ObjectId> HierarchyIndex::Subtree(ObjectId id) const
{
    std::vector<ObjectId> order;
    if (!nodes.contains(id))
    {
        return order;
    }
    std::vector<ObjectId> stack{id};
    while (!stack.empty())
    {
        const ObjectId next = stack.back();
        stack.pop_back();
        order.push_back(next);
        const std::vector<ObjectId>& children = nodes.at(next).children;
        stack.insert(stack.end(), children.rbegin(), children.rend());
    }
    return order;
}

bool HierarchyIndex::IsWithin(ObjectId id, ObjectId ancestor) const
{
    // Parent links in the index are acyclic (loops were broken when it was built).
    for (ObjectId at = id; at != 0;)
    {
        if (at == ancestor)
        {
            return true;
        }
        const auto found = nodes.find(at);
        at = found == nodes.end() ? 0 : found->second.parent;
    }
    return false;
}

HierarchyIndex BuildHierarchy(World& world)
{
    HierarchyIndex index;
    Query().With<StableIdComponent>().ForEachChunk(world,
                                                   [&](ChunkView& view)
                                                   {
                                                       const auto ids = view.Read<StableIdComponent>();
                                                       const auto entities = view.Entities();
                                                       for (std::size_t i = 0; i < ids.size(); ++i)
                                                       {
                                                           index.nodes[ids[i].id].entity = entities[i];
                                                       }
                                                   });
    for (auto& [id, node] : index.nodes)
    {
        const auto* parent = world.Get<ParentComponent>(node.entity);
        node.parent = parent == nullptr ? 0 : parent->parent.id;
        if (node.parent != 0 && !index.nodes.contains(node.parent))
        {
            index.broken.push_back(id);
            node.parent = 0;
        }
    }
    // Break loops: walk up from each object; an object whose chain comes back to itself becomes a root (the lowest
    // id of a loop is cut first, since nodes are visited in id order).
    for (auto& [id, node] : index.nodes)
    {
        std::size_t steps = 0;
        for (ObjectId at = node.parent; at != 0; at = index.nodes.at(at).parent)
        {
            if (at == id)
            {
                index.broken.push_back(id);
                node.parent = 0;
                break;
            }
            if (++steps > index.nodes.size())
            {
                break; // leads into a loop it isn't part of: the loop is cut when its lowest member is visited
            }
        }
    }
    std::ranges::sort(index.broken);
    for (auto& [id, node] : index.nodes)
    {
        if (node.parent == 0)
        {
            index.roots.push_back(id);
        }
        else
        {
            index.nodes.at(node.parent).children.push_back(id); // ids ascend, so children stay sorted
        }
    }
    return index;
}

Transform WorldTransformOf(World& world, const HierarchyIndex& index, ObjectId id)
{
    Transform result;
    for (ObjectId at = id; at != 0;)
    {
        const auto found = index.nodes.find(at);
        if (found == index.nodes.end())
        {
            break;
        }
        if (const auto* local = world.Get<LocalTransformComponent>(found->second.entity))
        {
            result = local->ToTransform() * result;
        }
        at = found->second.parent;
    }
    return result;
}

HierarchyIndex PropagateTransforms(World& world)
{
    HierarchyIndex index = BuildHierarchy(world);
    std::map<ObjectId, Transform> worlds;
    for (const ObjectId id : index.PreOrder())
    {
        const HierarchyIndex::Node& node = index.nodes.at(id);
        const auto* local = world.Get<LocalTransformComponent>(node.entity);
        const Transform localTransform = local == nullptr ? Transform{} : local->ToTransform();
        const Transform worldTransform = node.parent == 0 ? localTransform : worlds.at(node.parent) * localTransform;
        worlds.emplace(id, worldTransform);
        if (auto* transform = world.GetMut<Transform>(node.entity))
        {
            if (!(*transform == worldTransform))
            {
                *transform = worldTransform;
            }
        }
        else
        {
            (void)world.Add(node.entity, worldTransform);
        }
    }
    return index;
}

} // namespace interi::scene
