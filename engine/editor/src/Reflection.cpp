// SPDX-License-Identifier: MIT
#include <interi/editor/LevelBridge.h>
#include <interi/editor/Reflection.h>
#include <interi/gameplay/Vehicle.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Prefab.h>
#include <interi/scene/Primitives.h>
#include <interi/scene/SceneComponents.h>
#include <interi/scene/SceneRuntime.h>
#include <interi/scene/Triggers.h>

namespace interi::editor
{

void RegisterEngineReflection(ReflectionRegistry& registry)
{
    scene::RegisterSceneReflection(registry);
    scene::RegisterPrimitiveReflection(registry);
    scene::RegisterMeshReflection(registry);
    scene::RegisterPrefabReflection(registry);
    scene::RegisterChainsReflection(registry);
    scene::RegisterTriggerReflection(registry);
    RegisterLevelReflection(registry);
    registry.Register<gameplay::VehicleComponent>({Field("health", &gameplay::VehicleComponent::health)});
    registry.Register<gameplay::WreckComponent>({Field("wheel", &gameplay::WreckComponent::wheel)});
}

} // namespace interi::editor
