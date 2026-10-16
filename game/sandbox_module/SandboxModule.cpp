// SPDX-License-Identifier: MIT
// The sandbox's hot-reloadable gameplay rules (see interi/hotreload/ModuleApi.h). Edit, rebuild the
// interi_sandbox_module target (or run the sandbox with --hot-reload 1, which rebuilds it when this directory
// changes), and the running sandbox swaps the new code in without losing its world.
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/hotreload/ModuleApi.h>

#include "SandboxComponents.h"

namespace
{

using interi::ChunkView;
using interi::Query;
using interi::SystemAccess;
using interi::SystemContext;

/// Counts debris lifetimes down and destroys the expired ones.
void Expire(SystemContext& context)
{
    static const Query clock = Query{}.With<sandbox::StepTime>();
    static const Query aging = Query{}.With<sandbox::Lifetime>();
    float dt = 0.0f;
    context.ForEachChunk(clock,
                         [&dt](ChunkView& view)
                         {
                             for (const sandbox::StepTime& time : view.Read<sandbox::StepTime>())
                             {
                                 dt = time.seconds;
                             }
                         });
    context.ParallelForEachChunk(aging,
                                 [&context, dt](ChunkView& view, std::size_t ordinal)
                                 {
                                     auto lifetimes = view.Write<sandbox::Lifetime>();
                                     const auto entities = view.Entities();
                                     for (std::size_t i = 0; i < lifetimes.size(); ++i)
                                     {
                                         lifetimes[i].seconds -= dt;
                                         if (lifetimes[i].seconds <= 0.0f)
                                         {
                                             context.Commands().DestroyEntity(context.CommandKey(ordinal), entities[i]);
                                         }
                                     }
                                 });
}

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    interi::hotreload::DeclareComponent<sandbox::Lifetime>(registrar);
    interi::hotreload::DeclareComponent<sandbox::StepTime>(registrar);
    registrar.AddSystem("Expire", SystemAccess{}.Read<sandbox::StepTime>().Write<sandbox::Lifetime>(), &Expire);
    INTERI_LOG_INFO("Sandbox", "sandbox gameplay module registered");
    return true;
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
