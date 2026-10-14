// SPDX-License-Identifier: MIT
// Sandbox: an SDL window, input mapped to actions, an ECS world advanced by a fixed-step clock with Jolt physics,
// and the Vulkan forward renderer drawing the debris (shadows, point lights, GPU culling).
//
//   interi_sandbox [--video-driver <name>] [--frames <count>] [--validation 1] [--auto-spawn 1]
//
// WASD / left stick moves the spawn point, Space / South button spawns a burst of debris rigid bodies that fall onto
// the ground and pillars and expire after a few seconds, Escape quits.
// The window title shows the entity count and frame rate. --frames exits after that many frames (for smoke runs;
// with --video-driver dummy there is no Vulkan, so nothing is drawn, and --video-driver offscreen renders without
// a display). --auto-spawn keeps spawning debris without input, to exercise rendering in smoke runs.
#include <interi/core/Log.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/Scheduler.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/runtime/FixedStepClock.h>

#include "SandboxRenderer.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
        const std::uint32_t steps = clock.Advance(now - previous);
        previous = now;
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
            scheduler.Run(world, jobs, commands);
            physicsSync.Step(world, jobs, dt);
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
            presented += renderer->Render(debris, std::chrono::duration<double>(now - start).count()) ? 1 : 0;
        }

        ++framesSinceTitle;
        if (now - titleTime >= std::chrono::seconds(1))
        {
            const double seconds = std::chrono::duration<double>(now - titleTime).count();
            (*window)->SetTitle(std::format("Interi Sandbox - {} entities - {:.0f} fps", world.EntityCount(),
                                            static_cast<double>(framesSinceTitle) / seconds));
            titleTime = now;
            framesSinceTitle = 0;
        }
    }
    if (options.maxFrames != 0)
    {
        // Smoke runs check this: every frame should have been presented when rendering is enabled.
        std::printf("Presented %llu of %llu frames (%zu debris at exit)\n", static_cast<unsigned long long>(presented),
                    static_cast<unsigned long long>(options.maxFrames), debris.size());
    }
    interi::SetGlobalLogger(nullptr);
    return EXIT_SUCCESS;
}
