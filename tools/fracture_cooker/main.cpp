// SPDX-License-Identifier: MIT
// fracture_cooker <input.gltf|.glb> [-o output.idest] [--config file.fracture.json] [--materials materials.json]
//                 [--force] [--threads N]
#include <interi/destruction_cook/Cooker.h>
#include <interi/jobs/JobSystem.h>

#include <charconv>
#include <cstdio>
#include <optional>
#include <print>
#include <string_view>

namespace
{

void PrintUsage()
{
    std::println(stderr, "usage: fracture_cooker <input.gltf|.glb> [-o output.idest] [--config file.fracture.json]\n"
                         "                       [--materials materials.json] [--force] [--threads N]");
}

} // namespace

int main(int argc, char** argv)
{
    interi::destruction::CookRequest request;
    std::optional<std::uint32_t> threads;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if ((arg == "-o" || arg == "--output") && hasValue)
        {
            request.output = argv[++i];
        }
        else if (arg == "--config" && hasValue)
        {
            request.config = argv[++i];
        }
        else if (arg == "--materials" && hasValue)
        {
            request.materials = argv[++i];
        }
        else if (arg == "--force")
        {
            request.force = true;
        }
        else if (arg == "--threads" && hasValue)
        {
            const std::string_view value = argv[++i];
            std::uint32_t count = 0;
            if (std::from_chars(value.data(), value.data() + value.size(), count).ec != std::errc{})
            {
                PrintUsage();
                return 2;
            }
            threads = count;
        }
        else if (!arg.starts_with('-') && request.input.empty())
        {
            request.input = arg;
        }
        else
        {
            PrintUsage();
            return 2;
        }
    }
    if (request.input.empty())
    {
        PrintUsage();
        return 2;
    }

    interi::JobSystemDesc desc;
    if (threads)
    {
        desc.workerCount = *threads;
    }
    interi::JobSystem jobs(desc);
    const auto report = interi::destruction::Cook(request, jobs);
    if (!report)
    {
        std::println(stderr, "error: {} ({})", report.error().message, interi::ToString(report.error().code));
        return 1;
    }
    for (const std::string& warning : report->warnings)
    {
        std::println(stderr, "warning: {}", warning);
    }
    if (report->skipped)
    {
        std::println("up to date: {}", report->output.string());
    }
    else
    {
        std::println("cooked {}: {} chunks, {} bonds, {} anchors", report->output.string(), report->chunkCount,
                     report->bondCount, report->anchorCount);
    }
    return 0;
}
