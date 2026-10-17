// SPDX-License-Identifier: MIT
// Interi editor: builds destructible levels. A docking ImGui shell over the forward renderer: viewport (orbit camera:
// right mouse drag orbits, middle drag pans, wheel zooms), outliner, reflection-driven inspector and undo/redo.
// Levels are the JSON files under data/levels that the sandbox loads. The Fracture panel authors destructibles from
// glTF sources: their .fracture.json sidecars (seed levels, painted seed density, material volumes), a live preview
// cooked in memory, and Save & cook to the .idest the levels place. --fracture opens a source at startup.
//
//   level_editor [--level <path under data/>] [--fracture <glTF under data/>] [--video-driver <name>]
//                [--frames <count>] [--screenshot <file.ppm>] [--demo 1] [--validation 1]
//
// --frames exits after that many frames (smoke runs; --video-driver offscreen renders without a display, dummy has
// no Vulkan so the UI runs without drawing). --demo plays the level, blows up the middle wall, then saves and verifies
// a replay of it (the Structure panel's tools).
#include <interi/core/Log.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/platform_sdl/SdlPlatform.h>

#include "EditorApp.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

namespace
{

struct Options
{
    std::string level = "levels/sandbox.json";
    std::string fracture;
    std::string screenshot; ///< PPM of the last frame (with --frames).
    bool demo = false;
    std::string videoDriver;
    std::uint64_t maxFrames = 0; ///< 0 = run until quit.
    bool validation = false;
};

Options ParseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string_view flag = argv[i];
        if (flag == "--level")
        {
            options.level = argv[i + 1];
        }
        else if (flag == "--fracture")
        {
            options.fracture = argv[i + 1];
        }
        else if (flag == "--demo")
        {
            options.demo = std::string_view(argv[i + 1]) == "1";
        }
        else if (flag == "--screenshot")
        {
            options.screenshot = argv[i + 1];
        }
        else if (flag == "--video-driver")
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
    }
    return options;
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
    const interi::WindowDesc windowDesc{.title = "Interi Editor", .size = {1600, 900}, .vulkan = true};
    auto window = (*platform)->OpenWindow(windowDesc);
    if (!window)
    {
        window = (*platform)->OpenWindow(interi::WindowDesc{.title = "Interi Editor", .size = {1600, 900}});
    }
    if (!window)
    {
        std::fprintf(stderr, "%s\n", window.error().message.c_str());
        return EXIT_FAILURE;
    }

    int status = EXIT_SUCCESS;
    {
        interi::physics::JoltRuntime jolt; // play-in-editor and replays simulate with Jolt
        interi::JobSystem jobs;
        interi::editor::EditorApp app(**platform, **window, jobs,
                                      {.dataDir = INTERI_EDITOR_DATA_DIR, .validation = options.validation});
        if (auto opened = app.OpenLevel(options.level); !opened)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", options.level, opened.error().message);
        }
        if (!options.fracture.empty())
        {
            app.OpenFractureSource(options.fracture);
        }
        std::uint64_t presented = 0;
        std::uint64_t frame = 0;
        for (; options.maxFrames == 0 || frame < options.maxFrames; ++frame)
        {
            if (options.demo)
            {
                app.DemoStep(frame, options.maxFrames);
            }
            if (!options.screenshot.empty() && frame + 1 == options.maxFrames)
            {
                app.CaptureNextFrame(options.screenshot);
            }
            const interi::editor::FrameResult result = app.Frame();
            presented += result.presented ? 1 : 0;
            if (result.quit)
            {
                ++frame;
                break;
            }
        }
        if (options.maxFrames != 0)
        {
            // Smoke runs check this: every frame should have been presented when rendering is enabled.
            std::printf("Presented %llu of %llu frames (%zu entities in %s)\n",
                        static_cast<unsigned long long>(presented), static_cast<unsigned long long>(frame),
                        app.Document().Scene().Size(), app.Document().Path().c_str());
            if (!app.ReplayMessage().empty())
            {
                std::printf("Replay: %s\n", app.ReplayMessage().c_str());
            }
        }
    }
    interi::SetGlobalLogger(nullptr);
    return status;
}
