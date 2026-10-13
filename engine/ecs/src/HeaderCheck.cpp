// SPDX-License-Identifier: MIT
// Compiles every public ecs header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and instantiates the templates.
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>

namespace interi
{

namespace
{

struct HeaderCheckComponent
{
    int value;
};

struct HeaderCheckTag
{
};

// Instantiates the World templates once so clang-tidy checks their bodies. Never called.
[[maybe_unused]] void InstantiateWorld(World& world)
{
    const Entity entity = world.CreateEntity(HeaderCheckComponent{1}, HeaderCheckTag{});
    (void)world.Add(entity, HeaderCheckComponent{2});
    (void)world.Has<HeaderCheckTag>(entity);
    (void)world.Get<HeaderCheckComponent>(entity);
    (void)world.GetMut<HeaderCheckComponent>(entity);
    (void)world.Remove<HeaderCheckTag>(entity);
}

} // namespace

} // namespace interi
