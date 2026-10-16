// SPDX-License-Identifier: MIT
// Sandbox: an SDL window, input mapped to actions, an ECS world advanced by a fixed-step clock with Jolt physics and
// destruction, and the Vulkan forward renderer drawing it all (shadows, point lights, GPU culling, fractured
// walls with interior materials, GPU particles, crack and scorch decals).
//
//   interi_sandbox [--video-driver <name>] [--frames <count>] [--validation 1] [--auto-spawn 1]
//
// WASD / left stick moves the spawn point, Space / South button spawns a burst of debris rigid bodies that fall onto
// the ground and pillars and expire after a few seconds, E / East button sets off an explosion behind one of the
// three pre-fractured concrete walls (in turn), Escape quits.
// The window title shows the entity count and frame rate. --frames exits after that many frames (for smoke runs;
// with --video-driver dummy there is no Vulkan, so nothing is drawn, and --video-driver offscreen renders without
// a display). --auto-spawn keeps spawning debris and blasts a wall every second without input, to exercise
// rendering in smoke runs.
#include <interi/core/Log.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_render/BreakEffects.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/Scheduler.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/render/Decals.h>
#include <interi/runtime/FixedStepClock.h>

#include "SandboxRenderer.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Lifetime
{
    float seconds;
};

struct Options
{
    std::string videoDriver;
    std::uint64_t maxFrames = 0; ///< 0 = run until quit.
    bool validation = false;
    bool autoSpawn = false;
};

Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string_view flag = argv[i];
        if (flag == "--video-driver")
        {
            options.videoDriver = argv[i + 1];
        }
        else if (flag == "--frames")
        {
            options.maxFrames = std::strtoull(argv[i + 1], nullptr, 10);
        }
        else if (flag == "--validation")
        {
            options.validation = std::string_view(argv[i + 1]) == "1";
        }
        else if (flag == "--auto-spawn")
        {
            options.autoSpawn = std::string_view(argv[i + 1]) == "1";
        }
    }
    return options;
}

/// A static box entity (level geometry the debris lands on).
void SpawnStatic(interi::World& world, interi::physics::ObjectLayer layer, const interi::Vec3& center,
                 const interi::Vec3& halfExtents)
{
    interi::physics::RigidBodyComponent body;
    body.motion = interi::physics::MotionType::Static;
    body.layer = layer;
    interi::Transform transform;
    transform.position = center;
    (void)world.CreateEntity(body, interi::physics::ColliderComponent::Box(halfExtents), transform);
}

/// A 4 x 3 x 0.3 m concrete wall standing on y = 0, centred on x = z = 0, pre-fractured into 4 regions of up to 10
/// chunks each (as fracture_cooker would, without the glTF).
interi::Result<std::shared_ptr<const interi::destruction::DestructibleAsset>> BuildWall(interi::JobSystem& jobs)
{
    using namespace interi::destruction;
    HierarchySettings hierarchy;
    for (const auto& [count, seed] : {std::pair{4u, 11u}, std::pair{10u, 12u}})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = seed;
        hierarchy.levels.push_back(level);
    }
    const ChunkHierarchy chunks =
        BuildHierarchy(MakeBoxMesh({-2.0f, 0.0f, -0.15f}, {2.0f, 3.0f, 0.15f}), hierarchy, jobs);
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    BondSettings bonds;
    bonds.groundHeight = 0.0f;
    BondGraph graph = BuildBondGraph(chunks.chunks, materials, bonds);
    INTERI_TRY_ASSIGN(asset, AssembleAsset(chunks.chunks, std::move(graph), materials, {}, jobs));
    return std::make_shared<const DestructibleAsset>(std::move(asset));
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = ParseOptions(argc, argv);
    interi::Logger logger;
    logger.AddSink(std::make_unique<interi::ConsoleSink>());
    interi::SetGlobalLogger(&logger);
    auto platform = interi::SdlPlatform::Create(interi::SdlPlatformDesc{.videoDriver = options.videoDriver});
    if (!platform)
    {
        std::fprintf(stderr, "%s\n", platform.error().message.c_str());
        return EXIT_FAILURE;
    }
    auto window =
        (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Sandbox", .size = {1280, 720}, .vulkan = true});
    if (!window)
    {
        // No Vulkan in this video driver (e.g. "dummy"): run the simulation without drawing.
        window = (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Sandbox", .size = {1280, 720}});
    }
    if (!window)
    {
        std::fprintf(stderr, "%s\n", window.error().message.c_str());
        return EXIT_FAILURE;
    }

    interi::InputMapper input;
    const auto quit = input.AddAction("quit");
    input.BindKey(quit, interi::Key::Escape);
    const auto spawn = input.AddAction("spawn");
    input.BindKey(spawn, interi::Key::Space);
    input.BindGamepadButton(spawn, interi::GamepadButton::South);
    const auto explode = input.AddAction("explode");
    input.BindKey(explode, interi::Key::E);
    input.BindGamepadButton(explode, interi::GamepadButton::East);
    const auto moveX = input.AddAxis("moveX");
    input.BindKeys(moveX, interi::Key::A, interi::Key::D);
    input.BindGamepadAxis(moveX, interi::GamepadAxis::LeftX);
    const auto moveZ = input.AddAxis("moveZ");
    input.BindKeys(moveZ, interi::Key::W, interi::Key::S);
    input.BindGamepadAxis(moveZ, interi::GamepadAxis::LeftY);

    const interi::physics::JoltRuntime joltRuntime;
    interi::JobSystem jobs;
    std::unique_ptr<sandbox::SandboxRenderer> renderer;
    if (auto created = sandbox::SandboxRenderer::Create(**platform, **window, jobs, options.validation); created)
    {
        renderer = std::move(*created);
    }
    else
    {
        std::fprintf(stderr, "Rendering disabled: %s\n", created.error().message.c_str());
    }
    interi::CommandBuffers commands(jobs);
    interi::World world;
    interi::physics::PhysicsWorld physics(jobs);
    interi::physics::PhysicsSync physicsSync(physics);
    interi::FixedStepClock clock;
    const auto dt = static_cast<float>(clock.StepSeconds());
    interi::destruction::DestructionWorld destruction(physics);
    interi::destruction::DebrisSystem debrisSystem(physics);
    destruction.SetDebrisSystem(&debrisSystem);
    interi::destruction::BreakEffects breakEffects;

    // Three pre-fractured walls between the pillars.
    auto wall = BuildWall(jobs);
    if (!wall)
    {
        std::fprintf(stderr, "wall: %s\n", wall.error().message.c_str());
        return EXIT_FAILURE;
    }
    if (renderer != nullptr)
    {
        if (auto added = renderer->AddAsset(*wall); !added)
        {
            std::fprintf(stderr, "wall meshes: %s\n", added.error().message.c_str());
            return EXIT_FAILURE;
        }
    }
    constexpr std::array kWallX{-4.5f, 0.0f, 4.5f};
    for (const float x : kWallX)
    {
        interi::Transform transform;
        transform.position = {x, 0.0f, 0.0f};
        (void)destruction.Spawn(world, jobs, *wall, transform);
    }

    // Must match the scene SandboxRenderer draws: the ground plane and four 2 x 6 x 2 pillars.
    SpawnStatic(world, interi::physics::ObjectLayer::Static, {0.0f, -1.0f, 0.0f}, {40.0f, 1.0f, 40.0f});
    for (const interi::Vec3 pillar :
         {interi::Vec3{-6, 3, -6}, interi::Vec3{6, 3, -6}, interi::Vec3{-6, 3, 6}, interi::Vec3{6, 3, 6}})
    {
        SpawnStatic(world, interi::physics::ObjectLayer::Structure, pillar, {1.0f, 3.0f, 1.0f});
    }
    const interi::physics::ColliderComponent debrisCollider =
        interi::physics::ColliderComponent::Box(interi::Vec3(0.25f));

    interi::Query aging;
    aging.With<Lifetime>();
    interi::Query debrisQuery;
    debrisQuery.With<Lifetime, interi::Transform>();
    interi::Scheduler scheduler;
    (void)scheduler.AddSystem("Expire", interi::SystemAccess{}.Write<Lifetime>(),
                              [&](interi::SystemContext& context)
                              {
                                  context.ParallelForEachChunk(aging,
                                                               [&](interi::ChunkView& view, std::size_t ordinal)
                                                               {
                                                                   auto lifetime = view.Write<Lifetime>();
                                                                   const auto entities = view.Entities();
                                                                   for (std::uint32_t i = 0; i < view.Count(); ++i)
                                                                   {
                                                                       lifetime[i].seconds -= dt;
                                                                       if (lifetime[i].seconds <= 0.0f)
                                                                       {
                                                                           context.Commands().DestroyEntity(
                                                                               context.CommandKey(ordinal),
                                                                               entities[i]);
                                                                       }
                                                                   }
                                                               });
                              });
    if (auto built = scheduler.Build(); !built)
    {
        std::fprintf(stderr, "%s\n", built.error().message.c_str());
        return EXIT_FAILURE;
    }

    interi::Vec3 spawnPoint{0.0f, 10.0f, 0.0f};
    std::vector<interi::InputEvent> events;
    std::vector<interi::Transform> debris;
    std::vector<interi::render::Particle> particles;
    std::deque<interi::render::Decal> scorches;
    std::vector<interi::render::Decal> decals;
    std::uint64_t stepCount = 0;
    std::uint32_t blasts = 0;
    std::uint64_t presented = 0;
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    auto titleTime = previous;
    std::uint64_t framesSinceTitle = 0;
    for (std::uint64_t frame = 0; options.maxFrames == 0 || frame < options.maxFrames; ++frame)
    {
        events.clear();
        (*platform)->PollEvents(events);
        input.Update(events);
        if (input.QuitRequested() || input.WasPressed(quit))
        {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const float frameSeconds = std::chrono::duration<float>(now - previous).count();
        const std::uint32_t steps = clock.Advance(now - previous);
        previous = now;
        const double seconds = std::chrono::duration<double>(now - start).count();
        for (std::uint32_t step = 0; step < steps; ++step)
        {
            spawnPoint.x += input.AxisValue(moveX) * 5.0f * dt;
            spawnPoint.z += input.AxisValue(moveZ) * 5.0f * dt;
            if (input.IsDown(spawn) || options.autoSpawn)
            {
                for (int i = 0; i < 16; ++i)
                {
                    // Every fourth piece is large debris that piles up; the rest is rubble that only hits the level.
                    const float angle = static_cast<float>(step * 16 + static_cast<std::uint32_t>(i)) * 0.39f;
                    interi::physics::RigidBodyComponent body;
                    body.layer =
                        i % 4 == 0 ? interi::physics::ObjectLayer::Debris : interi::physics::ObjectLayer::DebrisSmall;
                    body.linearVelocity = {std::cos(angle) * 3.0f, 6.0f, std::sin(angle) * 3.0f};
                    interi::Transform transform;
                    transform.position = spawnPoint;
                    (void)world.CreateEntity(body, debrisCollider, transform, Lifetime{6.0f});
                }
            }
            if (input.WasPressed(explode) || (options.autoSpawn && stepCount % 60 == 30))
            {
                // Behind the wall (-z), so the blast throws its pieces out towards +z.
                const float x = kWallX[blasts % kWallX.size()] + 0.6f * static_cast<float>(blasts % 3) - 0.6f;
                const interi::Vec3 center{x, 1.0f + 0.4f * static_cast<float>(blasts % 4), -0.4f};
                destruction.QueueDamage({.center = center, .energy = 1.0e9f, .innerRadius = 0.8f, .radius = 1.3f},
                                        interi::destruction::DamageKind::Explosion);
                scorches.push_back(
                    {.transform = interi::render::DecalBox({center.x, center.y, 0.15f}, {0, 0, 1}, 2.2f, 2.2f),
                     .color = {0.02f, 0.02f, 0.02f},
                     .intensity = 0.8f,
                     .kind = interi::render::DecalKind::Scorch,
                     .seed = blasts});
                if (scorches.size() > 32)
                {
                    scorches.pop_front();
                }
                ++blasts;
            }
            scheduler.Run(world, jobs, commands);
            physicsSync.Step(world, jobs, dt);
            destruction.Update(world, jobs);
            interi::destruction::DebrisView debrisView;
            debrisView.position = sandbox::SandboxRenderer::CameraPosition(seconds);
            debrisSystem.Update(world, jobs, debrisView, dt);
            breakEffects.Spawn(destruction, particles);
            ++stepCount;
        }

        if (renderer != nullptr)
        {
            debris.clear();
            debrisQuery.ForEachChunk(world,
                                     [&](interi::ChunkView& view)
                                     {
                                         for (const interi::Transform& transform : view.Read<interi::Transform>())
                                         {
                                             debris.push_back(transform);
                                         }
                                     });
            decals.assign(scorches.begin(), scorches.end());
            presented += renderer->Render({.cubes = debris,
                                           .world = &world,
                                           .destruction = &destruction,
                                           .particles = particles,
                                           .decals = decals,
                                           .seconds = seconds,
                                           .dt = frameSeconds})
                             ? 1
                             : 0;
        }

        particles.clear();
        ++framesSinceTitle;
        if (now - titleTime >= std::chrono::seconds(1))
        {
            const double elapsed = std::chrono::duration<double>(now - titleTime).count();
            (*window)->SetTitle(std::format("Interi Sandbox - {} entities - {:.0f} fps", world.EntityCount(),
                                            static_cast<double>(framesSinceTitle) / elapsed));
            titleTime = now;
            framesSinceTitle = 0;
        }
    }
    if (options.maxFrames != 0)
    {
        // Smoke runs check this: every frame should have been presented when rendering is enabled.
        std::printf("Presented %llu of %llu frames (%zu debris at exit, %u blasts, %u debris pieces)\n",
                    static_cast<unsigned long long>(presented), static_cast<unsigned long long>(options.maxFrames),
                    debris.size(), blasts, debrisSystem.BodyCount() + debrisSystem.ParticleCount());
    }
    interi::SetGlobalLogger(nullptr);
    return EXIT_SUCCESS;
}
