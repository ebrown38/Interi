// SPDX-License-Identifier: MIT
// libFuzzer harness for every importer and cooked-format reader. The first input byte selects the target (see
// FuzzTargets.h), the rest is the file. Build with the Clang presets (target `interi_import_fuzzer`) and run
//   build/<preset>/engine/asset_import/interi_import_fuzzer tests/asset_import/fuzz/corpus -max_len=65536
#include "FuzzTargets.h"

#include <cstdio>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
    {
        return 0;
    }
    const auto target =
        static_cast<interi::fuzz::Target>(data[0] % static_cast<std::uint8_t>(interi::fuzz::Target::Count));
    const std::string problem =
        interi::fuzz::Run(target, std::span<const std::byte>(reinterpret_cast<const std::byte*>(data + 1), size - 1));
    if (!problem.empty())
    {
        std::fprintf(stderr, "%s: %s\n", std::string(interi::fuzz::Name(target)).c_str(), problem.c_str());
        std::abort();
    }
    return 0;
}
