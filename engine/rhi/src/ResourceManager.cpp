// SPDX-License-Identifier: MIT
#include <interi/rhi/ResourceManager.h>

#include <algorithm>

namespace interi::rhi
{

ResourceManager::ResourceManager(IDevice& device)
    : m_device(device)
{
}

ResourceManager::~ResourceManager()
{
    if (m_pending.empty())
    {
        return;
    }
    m_device.WaitForValue(m_pending.back().retireValue);
    for (const Pending& pending : m_pending)
    {
        Destroy(pending);
    }
}

void ResourceManager::DestroyBuffer(BufferHandle buffer)
{
    Defer(buffer);
}

void ResourceManager::DestroyTexture(TextureHandle texture)
{
    Defer(texture);
}

void ResourceManager::Defer(std::variant<BufferHandle, TextureHandle> resource)
{
    const std::scoped_lock lock(m_mutex);
    m_pending.push_back(Pending{.resource = resource, .retireValue = m_device.LastSubmittedValue() + 1});
}

void ResourceManager::Collect()
{
    const std::uint64_t completed = m_device.CompletedValue();
    std::vector<Pending> retired;
    {
        const std::scoped_lock lock(m_mutex);
        const auto firstLive = std::ranges::find_if(m_pending, [completed](const Pending& pending)
                                                    { return pending.retireValue > completed; });
        retired.assign(m_pending.begin(), firstLive);
        m_pending.erase(m_pending.begin(), firstLive);
    }
    for (const Pending& pending : retired)
    {
        Destroy(pending);
    }
}

std::size_t ResourceManager::PendingCount() const
{
    const std::scoped_lock lock(m_mutex);
    return m_pending.size();
}

void ResourceManager::Destroy(const Pending& pending)
{
    if (const auto* buffer = std::get_if<BufferHandle>(&pending.resource))
    {
        m_device.DestroyBuffer(*buffer);
    }
    else if (const auto* texture = std::get_if<TextureHandle>(&pending.resource))
    {
        m_device.DestroyTexture(*texture);
    }
}

} // namespace interi::rhi
