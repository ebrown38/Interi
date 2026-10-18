// SPDX-License-Identifier: MIT
#include <interi/scene/SceneComponents.h>

#include <utility>

namespace interi::scene
{

void RegisterSceneReflection(values::ReflectionRegistry& registry)
{
    using values::Field;
    registry.Register<NameComponent>({Field("name", &NameComponent::name)});
    registry.Register<ParentComponent>({Field("parent", &ParentComponent::parent)});
    registry.Register<LocalTransformComponent>({
        Field("position", &LocalTransformComponent::position),
        Field("rotation", &LocalTransformComponent::rotation),
        Field("scale", &LocalTransformComponent::scale),
    });
}

Entity CreateObject(World& world, ObjectId id, std::string name, ObjectId parent, const Transform& local)
{
    return world.CreateEntity(StableIdComponent{id}, NameComponent{std::move(name)},
                              ParentComponent{values::EntityRef{parent}}, LocalTransformComponent::From(local), local);
}

} // namespace interi::scene
