// SPDX-License-Identifier: MIT
#pragma once

// What the importer fuzz tests share: the targets (every parser that reads a file from outside), the seed corpus and
// the check each target makes on a successful parse. The deterministic mutation test (`stress` label, every compiler)
// and the libFuzzer harness (Clang) both drive these.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::fuzz
{

enum class Target : std::uint8_t
{
    Gltf,
    Obj,
    Fbx,
    Image,
    TextureImport,
    MeshAsset,
    TextureAsset,
    MaterialAsset,
    MaterialJson,
    SkeletonAsset,
    ClipAsset,
    SkinAsset,
    ModelManifest,
    Count,
};

[[nodiscard]] std::string_view Name(Target target) noexcept;

/// Parses `data` as `target` and checks what a successful parse promises (validators pass, round trips are exact).
/// Returns an empty string when all is well, otherwise what was violated. A parse error is fine; a crash is the bug.
[[nodiscard]] std::string Run(Target target, std::span<const std::byte> data);

struct Seed
{
    Target target;
    std::string name;
    std::vector<std::byte> data;
};

/// Small valid inputs for every target.
[[nodiscard]] std::vector<Seed> SeedCorpus();

} // namespace interi::fuzz
