// SPDX-License-Identifier: MIT
// interi_player: the runtime of an exported game, the same engine without the editor. It verifies the package, opens
// the game folder as a GameSession (modules, data, startup scene) and plays it. The launchers `export` writes run it.
//
//   interi_player --package <dir> [--steps <n>] [--realtime] [--press <action>@<step>]... [--print <value id>]...
//                 [--user-dir <dir>] [--workers <n>] [--verify-only]
//                 [--window [--video-driver <name>] [--frames <n>]]
//
// Without --realtime the steps run as fast as possible (headless smoke runs and CI); --realtime paces them to the
// fixed step clock. --press raises an input action before a step. Exit code: 0 played, 1 the package didn't verify
// or the game didn't open, 2 usage.
//
// --window opens an SDL window and plays in real time: keyboard, mouse and gamepad events go through the game's
// content/input.json bindings into the chains (RaiseInputAction), until the window closes or --frames frames have
// passed. Scene drawing is not part of the player yet: a renderer plugs into the same loop.
#include <interi/core/Log.h>
#include <interi/exporter/ProjectExport.h>
#include <interi/game/GameSession.h>
#include <interi/platform/InputConfig.h>
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/runtime/FixedStepClock.h>
#include <interi/values/Value.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

constexpr const char* kUsage =
    "usage: interi_player --package <dir> [--steps <n>] [--realtime] [--press <action>@<step>]...\n"
    "                     [--print <value id>]... [--user-dir <dir>] [--workers <n>] [--verify-only]\n"
    "                     [--window [--video-driver <name>] [--frames <n>]]\n";

struct Press
{
    std::string action;
    std::uint64_t step = 0;
};

} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path package;
    std::filesystem::path userDirectory;
    std::uint64_t steps = 600;
    std::uint32_t workers = interi::JobSystemDesc::kAutoWorkerCount;
    bool realtime = false;
    bool verifyOnly = false;
    bool window = false;
    std::string videoDriver;
    std::uint64_t frames = 0; // 0: until the window closes
    std::vector<Press> presses;
    std::vector<std::string> prints;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        const bool hasValue = i + 1 < argc;
        if (argument == "--package" && hasValue)
        {
            package = argv[++i];
        }
        else if (argument == "--user-dir" && hasValue)
        {
            userDirectory = argv[++i];
        }
        else if (argument == "--steps" && hasValue)
        {
            steps = std::strtoull(argv[++i], nullptr, 10);
        }
        else if (argument == "--workers" && hasValue)
        {
            workers = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
        else if (argument == "--press" && hasValue)
        {
            const std::string_view text = argv[++i];
            const std::size_t at = text.rfind('@');
            if (at == std::string_view::npos)
            {
                std::fputs(kUsage, stderr);
                return 2;
            }
            presses.push_back({std::string(text.substr(0, at)),
                               std::strtoull(std::string(text.substr(at + 1)).c_str(), nullptr, 10)});
        }
        else if (argument == "--print" && hasValue)
        {
            prints.emplace_back(argv[++i]);
        }
        else if (argument == "--video-driver" && hasValue)
        {
            videoDriver = argv[++i];
        }
        else if (argument == "--frames" && hasValue)
        {
            frames = std::strtoull(argv[++i], nullptr, 10);
        }
        else if (argument == "--window")
        {
            window = true;
        }
        else if (argument == "--realtime")
        {
            realtime = true;
        }
        else if (argument == "--verify-only")
        {
            verifyOnly = true;
        }
        else if (argument == "--help" || argument == "-h")
        {
            std::fputs(kUsage, stdout);
            return 0;
        }
        else
        {
            std::fputs(kUsage, stderr);
            return 2;
        }
    }
    if (package.empty())
    {
        std::fputs(kUsage, stderr);
        return 2;
    }

    auto info = interi::exporter::VerifyPackage(package);
    if (!info)
    {
        std::fprintf(stderr, "package '%s' is not usable: %s\n", package.string().c_str(),
                     info.error().message.c_str());
        return 1;
    }
    std::printf("Package '%s': %zu files, %zu modules, verified\n", info->gameName.c_str(), info->fileCount,
                info->modules.size());
    if (verifyOnly)
    {
        return 0;
    }

    interi::game::GameDesc desc;
    desc.projectDirectory = info->gameDirectory;
    desc.modules.librarySearchPaths = info->libraryDirectories;
    desc.workerCount = workers;
    desc.saveDirectory = userDirectory;
    auto opened = interi::game::GameSession::Open(desc);
    if (!opened)
    {
        std::fprintf(stderr, "the game didn't open: %s\n", opened.error().message.c_str());
        return 1;
    }
    interi::game::GameSession& session = **opened;
    for (const std::string& warning : session.Warnings())
    {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    interi::FixedStepClock clock;
    auto last = std::chrono::steady_clock::now();
    if (window)
    {
        auto platform = interi::SdlPlatform::Create({.videoDriver = videoDriver});
        if (!platform)
        {
            std::fprintf(stderr, "no window: %s\n", platform.error().message.c_str());
            return 1;
        }
        auto opened_window = (*platform)->OpenWindow({.title = info->gameName});
        if (!opened_window)
        {
            std::fprintf(stderr, "no window: %s\n", opened_window.error().message.c_str());
            return 1;
        }
        interi::InputMapper mapper;
        if (auto config = session.Files().ReadFile("content/input.json"))
        {
            const std::string text(reinterpret_cast<const char*>(config->data()), config->size());
            if (auto applied = interi::ApplyInputConfig(text, mapper, interi::InputMerge::Add); !applied)
            {
                std::fprintf(stderr, "warning: input config: %s\n", applied.error().message.c_str());
            }
        }
        std::vector<interi::InputEvent> events;
        std::uint64_t played = 0;
        for (std::uint64_t frame = 0; frames == 0 || frame < frames; ++frame)
        {
            events.clear();
            (*platform)->PollEvents(events);
            mapper.Update(events);
            if (mapper.QuitRequested())
            {
                break;
            }
            for (std::size_t action = 0; action < mapper.ActionCount(); ++action)
            {
                const auto id = static_cast<interi::InputMapper::ActionId>(action);
                if (mapper.WasPressed(id))
                {
                    session.RaiseInputAction(mapper.ActionName(id));
                }
            }
            const auto now = std::chrono::steady_clock::now();
            for (std::uint32_t n = clock.Advance(now - last); n > 0; --n)
            {
                session.Step();
                ++played;
            }
            last = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        steps = played;
    }
    for (std::uint64_t step = 0; !window && step < steps; ++step)
    {
        for (const Press& press : presses)
        {
            if (press.step == step)
            {
                session.RaiseInputAction(press.action);
            }
        }
        session.Step();
        if (realtime)
        {
            std::this_thread::sleep_for(clock.Step());
            const auto now = std::chrono::steady_clock::now();
            (void)clock.Advance(now - last);
            last = now;
        }
    }
    std::printf("Played %llu steps\n", static_cast<unsigned long long>(steps));
    for (const std::string& id : prints)
    {
        const interi::values::Value* value = session.Values().Read(id);
        std::printf("%s=%s\n", id.c_str(), value != nullptr ? interi::values::FormatValue(*value).c_str() : "<unset>");
    }
    return 0;
}
