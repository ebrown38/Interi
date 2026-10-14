// SPDX-License-Identifier: MIT
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace interi::testing
{

/// An 8-bit RGBA image, rows top to bottom.
struct Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::array<std::uint8_t, 4>> pixels;
};

/// How far a render may drift from its golden image. Software rasterizers (lavapipe) JIT for the host CPU, so
/// tiny differences in transcendental math are expected between machines.
struct GoldenTolerance
{
    int perChannel = 3;                  ///< Largest allowed difference of one channel (0-255).
    double maxDifferingFraction = 0.002; ///< Pixels allowed to exceed perChannel.
};

/// Binary PPM (P6, RGB): uncompressed and readable by most image viewers.
inline bool WritePpm(const std::filesystem::path& path, const Image& image)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (const auto& pixel : image.pixels)
    {
        file.write(reinterpret_cast<const char*>(pixel.data()), 3);
    }
    return static_cast<bool>(file);
}

inline std::optional<Image> ReadPpm(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::string magic;
    Image image;
    int maxValue = 0;
    if (!(file >> magic >> image.width >> image.height >> maxValue) || magic != "P6" || maxValue != 255)
    {
        return std::nullopt;
    }
    file.get(); // the single whitespace after the header
    image.pixels.resize(std::size_t{image.width} * image.height);
    for (auto& pixel : image.pixels)
    {
        file.read(reinterpret_cast<char*>(pixel.data()), 3);
        pixel[3] = 255;
    }
    return file ? std::optional<Image>(std::move(image)) : std::nullopt;
}

/// Compares `actual` with tests/render/golden/<name>.ppm. On mismatch the actual image and a diff (differing
/// pixels in red) are written to the build's golden-output directory. Set INTERI_UPDATE_GOLDEN=1 to (re)write the
/// golden file from `actual` instead.
inline void ExpectMatchesGolden(const Image& actual, const std::string& name, GoldenTolerance tolerance = {})
{
    const std::filesystem::path golden = std::filesystem::path(INTERI_GOLDEN_DIR) / (name + ".ppm");
    const std::filesystem::path output = std::filesystem::path(INTERI_GOLDEN_OUTPUT_DIR);
#if defined(_MSC_VER)
#pragma warning(suppress : 4996)
#endif
    const char* update = std::getenv("INTERI_UPDATE_GOLDEN");
    if (update != nullptr && std::string(update) == "1")
    {
        ASSERT_TRUE(WritePpm(golden, actual));
        GTEST_SKIP() << "golden image written: " << golden;
    }

    const std::optional<Image> expected = ReadPpm(golden);
    ASSERT_TRUE(expected.has_value()) << "missing golden " << golden << " (run with INTERI_UPDATE_GOLDEN=1)";
    ASSERT_EQ(expected->width, actual.width);
    ASSERT_EQ(expected->height, actual.height);

    Image diff{actual.width, actual.height, std::vector<std::array<std::uint8_t, 4>>(actual.pixels.size())};
    std::size_t differing = 0;
    int worst = 0;
    for (std::size_t i = 0; i < actual.pixels.size(); ++i)
    {
        int channel = 0;
        for (std::size_t c = 0; c < 3; ++c)
        {
            channel = std::max(channel, std::abs(int{actual.pixels[i][c]} - int{expected->pixels[i][c]}));
        }
        worst = std::max(worst, channel);
        const bool bad = channel > tolerance.perChannel;
        differing += bad ? 1 : 0;
        const auto grey = static_cast<std::uint8_t>(expected->pixels[i][1] / 4);
        diff.pixels[i] =
            bad ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{grey, grey, grey, 255};
    }
    const double fraction = static_cast<double>(differing) / static_cast<double>(actual.pixels.size());
    if (fraction > tolerance.maxDifferingFraction)
    {
        WritePpm(output / (name + ".actual.ppm"), actual);
        WritePpm(output / (name + ".diff.ppm"), diff);
        ADD_FAILURE() << name << ": " << differing << " pixels (" << fraction * 100.0 << "%) differ by more than "
                      << tolerance.perChannel << " (worst " << worst << "). Actual and diff written to " << output;
    }
}

} // namespace interi::testing
