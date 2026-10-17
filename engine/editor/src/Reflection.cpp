// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/gameplay/Vehicle.h>

namespace interi::editor
{

void RegisterEngineReflection(ReflectionRegistry& registry)
{
    registry.Register<Transform>({
        Field("position", &Transform::position),
        Field("rotation", &Transform::rotation),
        Field("scale", &Transform::scale),
    });
    registry.Register<StaticBoxComponent>({Field("halfExtents", &StaticBoxComponent::halfExtents)});
    registry.Register<DestructiblePlacementComponent>({Field("asset", &DestructiblePlacementComponent::asset)});
    registry.Register<SpawnPointComponent>({Field("name", &SpawnPointComponent::name)});
    registry.Register<gameplay::VehicleComponent>({Field("health", &gameplay::VehicleComponent::health)});
    registry.Register<gameplay::WreckComponent>({Field("wheel", &gameplay::WreckComponent::wheel)});
}

} // namespace interi::editor
