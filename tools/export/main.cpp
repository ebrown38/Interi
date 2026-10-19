// SPDX-License-Identifier: MIT
// interi_export: cooks a project into a package folder (see exporter/ProjectExport.h).
//
//   interi_export <project dir> <output dir> [--lib <dir>]... [--runtime <file or dir>]...
//
// --lib adds a folder searched for module libraries not found next to their module.json (a build output directory);
// --runtime adds files for bin/ (the interi_player executable and the engine libraries; a directory contributes its
// files). Exit code: 0 exported, 1 failed, 2 usage.
#include <interi/exporter/ProjectExport.h>

#include <cstdio>
#include <string_view>

namespace
{
constexpr const char* kUsage =
    "usage: interi_export <project dir> <output dir> [--lib <dir>]... [--runtime <file or dir>]...\n";
}

int main(int argc, char** argv)
{
    interi::exporter::ExportDesc desc;
    int positional = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--lib" && i + 1 < argc)
        {
            desc.librarySearchPaths.emplace_back(argv[++i]);
        }
        else if (argument == "--runtime" && i + 1 < argc)
        {
            desc.runtimeFiles.emplace_back(argv[++i]);
        }
        else if (argument == "--help" || argument == "-h")
        {
            std::fputs(kUsage, stdout);
            return 0;
        }
        else if (!argument.starts_with("-") && positional < 2)
        {
            (positional++ == 0 ? desc.projectDirectory : desc.outputDirectory) = argv[i];
        }
        else
        {
            std::fputs(kUsage, stderr);
            return 2;
        }
    }
    if (positional != 2)
    {
        std::fputs(kUsage, stderr);
        return 2;
    }
    auto report = interi::exporter::ExportProject(desc);
    if (!report)
    {
        std::fprintf(stderr, "export failed: %s\n", report.error().message.c_str());
        return 1;
    }
    for (const std::string& warning : report->warnings)
    {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }
    std::printf("Exported '%s': %zu modules, %zu files to %s\n", report->gameName.c_str(), report->modules.size(),
                report->files.size(), desc.outputDirectory.string().c_str());
    return 0;
}
