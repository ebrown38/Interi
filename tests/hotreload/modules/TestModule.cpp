// SPDX-License-Identifier: MIT
// One source, several hot-reload test modules (INTERI_TEST_MODULE_VARIANT):
//   1: Counter += 1 per run, marks counters at the threshold, Health v1 regenerates by 1.
//   2: like 1 with Counter += 10 and an extra system "Extra".
//   3: Health v2 (maxHp) with a migrator from v1.
//   4: Health v2 without a migrator (a reload over v1 health is rejected).
//   5: refuses to load.
//   6: built for another module ABI.
//   7: reports another toolchain fingerprint.
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/hotreload/ModuleApi.h>

#include "TestComponents.h"

#include <algorithm>
#include <cstring>

#ifndef INTERI_TEST_MODULE_VARIANT
#error "define INTERI_TEST_MODULE_VARIANT"
#endif

namespace
{

using interi::ChunkView;
using interi::Query;
using interi::SystemAccess;
using interi::SystemContext;
using interi::hotreload::ModuleRegistrar;
using interi::testing::HrCounter;
using interi::testing::HrMarked;

constexpr int kVariant = INTERI_TEST_MODULE_VARIANT;
constexpr std::int32_t kStep = kVariant == 2 ? 10 : 1;

#if INTERI_TEST_MODULE_VARIANT >= 3
using Health = interi::testing::HrHealthV2;
#else
using Health = interi::testing::HrHealthV1;
#endif

#if INTERI_TEST_MODULE_VARIANT == 3
bool MigrateHealth(std::uint32_t fromVersion, const void* from, std::size_t fromSize, void* to)
{
    if (fromVersion != 1 || fromSize != sizeof(interi::testing::HrHealthV1))
    {
        return false;
    }
    interi::testing::HrHealthV1 old;
    std::memcpy(&old, from, sizeof(old));
    const Health migrated{.hp = old.hp, .maxHp = interi::testing::kMigratedMaxHp};
    std::memcpy(to, &migrated, sizeof(migrated));
    return true;
}
#endif

[[maybe_unused]] void Increment(SystemContext& context)
{
    static const Query query = Query{}.With<HrCounter>();
    context.ParallelForEachChunk(query,
                                 [](ChunkView& chunk, std::size_t)
                                 {
                                     for (HrCounter& counter : chunk.Write<HrCounter>())
                                     {
                                         counter.value += kStep;
                                     }
                                 });
}

// Structural changes from worker threads: exercises CommandBuffers::Local from module code.
[[maybe_unused]] void Mark(SystemContext& context)
{
    static const Query query = Query{}.With<HrCounter>().Without<HrMarked>();
    context.ParallelForEachChunk(query,
                                 [&context](ChunkView& chunk, std::size_t ordinal)
                                 {
                                     const auto counters = chunk.Read<HrCounter>();
                                     const auto entities = chunk.Entities();
                                     for (std::size_t i = 0; i < counters.size(); ++i)
                                     {
                                         if (counters[i].value >= interi::testing::kMarkThreshold)
                                         {
                                             context.Commands().Add(context.CommandKey(ordinal), entities[i],
                                                                    HrMarked{});
                                         }
                                     }
                                 });
}

[[maybe_unused]] void Regenerate(SystemContext& context)
{
    static const Query query = Query{}.With<Health>();
    context.ForEachChunk(query,
                         [](ChunkView& chunk)
                         {
                             for (Health& health : chunk.Write<Health>())
                             {
#if INTERI_TEST_MODULE_VARIANT >= 3
                                 health.hp = std::min(health.hp + 1.0f, health.maxHp);
#else
                                 health.hp += 1.0f;
#endif
                             }
                         });
}

#if INTERI_TEST_MODULE_VARIANT == 5
bool Register(ModuleRegistrar&)
{
    return false; // (a preprocessor branch: MSVC reports code after an `if constexpr` return as unreachable)
}
#else
bool Register(ModuleRegistrar& registrar)
{
    interi::hotreload::DeclareComponent<HrCounter>(registrar);
    interi::hotreload::DeclareComponent<HrMarked>(registrar);
#if INTERI_TEST_MODULE_VARIANT == 3
    interi::hotreload::DeclareComponent<Health>(registrar, &MigrateHealth);
#else
    interi::hotreload::DeclareComponent<Health>(registrar);
#endif
    registrar.AddSystem("Mark", SystemAccess{}.Read<HrCounter>().Write<HrMarked>(), &Mark);
    registrar.AddSystem("Increment", SystemAccess{}.Write<HrCounter>(), &Increment);
    registrar.RunAfter("Mark", "Increment");
    registrar.AddSystem("Regenerate", SystemAccess{}.Write<Health>(), &Regenerate);
    if constexpr (kVariant == 2)
    {
        registrar.AddSystem("Extra", SystemAccess{}, [](SystemContext&) {});
    }
    return true;
}
#endif

} // namespace

#if INTERI_TEST_MODULE_VARIANT == 6 || INTERI_TEST_MODULE_VARIANT == 7
INTERI_MODULE_EXPORT const interi::hotreload::ModuleApi* InteriGetModuleApi()
{
    static const interi::hotreload::ModuleApi api{
        .abiVersion = interi::hotreload::kModuleAbiVersion + (kVariant == 6 ? 1000 : 0),
        .toolchain = kVariant == 7 ? "abi2;another-compiler" : INTERI_TOOLCHAIN_FINGERPRINT,
        .load = &interi::hotreload::detail::LoadModule<&Register>,
        .unload = &interi::hotreload::detail::UnloadModule,
    };
    return &api;
}
#else
INTERI_GAMEPLAY_MODULE(Register)
#endif
