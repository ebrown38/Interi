// SPDX-License-Identifier: MIT
// Minimal sandbox: an SDL window, input mapped to actions, and an ECS world advanced by a fixed-step clock.
//
//   interi_sandbox [--video-driver <name>] [--frames <count>]
//
// WASD / left stick moves the spawn point, Space / South button spawns a burst of debris entities, Escape quits.
// The window title shows the entity count and frame rate. --frames exits after that many frames (for smoke runs
// with --video-driver dummy).
#include <interi/core/Log.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/Scheduler.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/runtime/FixedStepClock.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace
{

struct Position
{
    interi::Vec3 value;
};

struct Velocity
{
    interi::Vec3 value;
};

struct Lifetime
{
    float seconds;
};

struct Options
{
    std::string videoDriver;
    std::uint64_t maxFrames = 0; ///< 0 = run until quit.
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
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = ParseOptions(argc, argv);
    auto platform = interi::SdlPlatform::Create(interi::SdlPlatformDesc{.videoDriver = options.videoDriver});
    if (!platform)
    {
        std::fprintf(stderr, "%s\n", platform.error().message.c_str());
        return EXIT_FAILURE;
    }
    auto window = (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Sandbox", .size = {1280, 720}});
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

    interi::JobSystem jobs;
    interi::CommandBuffers commands(jobs);
    interi::World world;
    interi::FixedStepClock clock;
    const auto dt = static_cast<float>(clock.StepSeconds());

    interi::Query moving;
    moving.With<Position, Velocity>();
    interi::Query aging;
    aging.With<Lifetime>();
    interi::Scheduler scheduler;
    (void)scheduler.AddSystem("Gravity", interi::SystemAccess{}.Write<Velocity>(),
                              [&](interi::SystemContext& context)
                              {
                                  context.ParallelForEachChunk(moving,
                                                               [dt](interi::ChunkView& view, std::size_t)
                                                               {
                                                                   for (Velocity& velocity : view.Write<Velocity>())
                                                                   {
                                                                       velocity.value.y -= 9.81f * dt;
                                                                   }
                                                               });
                              });
    (void)scheduler.AddSystem("Integrate", interi::SystemAccess{}.Read<Velocity>().Write<Position>(),
                              [&](interi::SystemContext& context)
                              {
                                  context.ParallelForEachChunk(moving,
                                                               [dt](interi::ChunkView& view, std::size_t)
                                                               {
                                                                   const auto velocity = view.Read<Velocity>();
                                                                   auto position = view.Write<Position>();
                                                                   for (std::uint32_t i = 0; i < view.Count(); ++i)
                                                                   {
                                                                       position[i].value += velocity[i].value * dt;
                                                                   }
                                                               });
                              });
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
    auto previous = std::chrono::steady_clock::now();
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
            if (input.IsDown(spawn))
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float angle = static_cast<float>(i) * 0.0245f;
                    (void)world.CreateEntity(
                        Position{spawnPoint},
                        Velocity{interi::Vec3{std::cos(angle) * 3.0f, 6.0f, std::sin(angle) * 3.0f}}, Lifetime{3.0f});
                }
            }
            scheduler.Run(world, jobs, commands);
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
    return EXIT_SUCCESS;
}
