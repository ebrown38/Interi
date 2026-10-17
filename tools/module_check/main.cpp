// SPDX-License-Identifier: MIT
// interi_module_check: loads a project of gameplay modules through the engine's module host and reports what each
// one contributes. Module authors run it (and the scaffolded load test does) to check that a module builds against
// this engine, resolves, and registers.
//
//   interi_module_check [--search <library dir>]... <module dir>...
//
// Every <module dir> holds a module.json; the project order is the order given (first = highest priority).
// Libraries are looked up in each module's folder, then in the --search directories. Exit code 0 when the project
// loads.
#include <interi/core/Log.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleHost.h>
#include <interi/sdk/Manifest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{

constexpr const char* kUsage = "usage: interi_module_check [--search <library dir>]... <module dir>...\n";

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::filesystem::path> searchPaths;
    std::vector<std::filesystem::path> moduleDirectories;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--search" && i + 1 < argc)
        {
            searchPaths.emplace_back(argv[++i]);
        }
        else if (argument == "--help" || argument == "-h")
        {
            std::fputs(kUsage, stdout);
            return 0;
        }
        else if (argument.starts_with("-"))
        {
            std::fputs(kUsage, stderr);
            return 2;
        }
        else
        {
            moduleDirectories.emplace_back(argument);
        }
    }
    if (moduleDirectories.empty())
    {
        std::fputs(kUsage, stderr);
        return 2;
    }

    std::vector<interi::sdk::ModuleManifest> manifests;
    std::vector<std::string> project;
    for (const std::filesystem::path& directory : moduleDirectories)
    {
        auto manifest = interi::sdk::LoadManifest(directory);
        if (!manifest)
        {
            std::fprintf(stderr, "error: %s\n", manifest.error().message.c_str());
            return 1;
        }
        for (const std::string& warning : manifest->warnings)
        {
            std::fprintf(stderr, "warning: %s\n", warning.c_str());
        }
        project.push_back(manifest->id);
        manifests.push_back(std::move(*manifest));
    }

    interi::World world;
    interi::hotreload::ModuleHostDesc desc;
    desc.librarySearchPaths = std::move(searchPaths);
    interi::hotreload::ModuleHost host(world, desc);
    auto loaded = host.LoadProject(project, manifests);
    if (!loaded)
    {
        std::fprintf(stderr, "error: %s\n", loaded.error().message.c_str());
        return 1;
    }

    std::printf("loaded %zu module(s), %u system(s):\n", host.LoadedModules().size(), loaded->systems);
    for (const std::string& id : host.LoadedModules())
    {
        std::printf("  %s\n", id.c_str());
    }
    for (const std::string& id : host.Hooks().Registry().Ids())
    {
        std::printf("hook %s:", id.c_str());
        for (const auto& entry : host.Hooks().Registry().Provenance(id))
        {
            std::printf(" %s%s", entry.provider.c_str(), entry.isOverride ? " (override)" : "");
        }
        std::printf("\n");
    }
    return 0;
}
