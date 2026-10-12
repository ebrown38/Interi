// SPDX-License-Identifier: MIT
#include <interi/testing/ScopedTempDir.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <random>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace interi::testing
{

namespace
{
std::uint64_t NextUniqueToken()
{
    // Random base distinguishes concurrent test processes; the counter distinguishes instances.
    static const std::uint64_t base =
        (static_cast<std::uint64_t>(std::random_device{}()) << 32) ^ std::random_device{}();
    static std::atomic<std::uint64_t> counter{0};
    return base + counter.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

ScopedTempDir::ScopedTempDir()
{
    const std::filesystem::path root = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        std::filesystem::path candidate = root / std::format("interi-test-{:016x}", NextUniqueToken());
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec))
        {
            m_path = std::move(candidate);
            return;
        }
    }
    throw std::runtime_error("ScopedTempDir: could not create a unique temporary directory");
}

ScopedTempDir::~ScopedTempDir()
{
    Remove();
}

ScopedTempDir::ScopedTempDir(ScopedTempDir&& other) noexcept
    : m_path(std::exchange(other.m_path, {}))
{
}

ScopedTempDir& ScopedTempDir::operator=(ScopedTempDir&& other) noexcept
{
    if (this != &other)
    {
        Remove();
        m_path = std::exchange(other.m_path, {});
    }
    return *this;
}

void ScopedTempDir::Remove() noexcept
{
    if (!m_path.empty())
    {
        std::error_code ec; // best effort: never throw from a destructor
        std::filesystem::remove_all(m_path, ec);
        m_path.clear();
    }
}

} // namespace interi::testing
