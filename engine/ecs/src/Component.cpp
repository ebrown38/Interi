// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/ecs/Component.h>

#include <array>
#include <atomic>
#include <mutex>

namespace interi
{

namespace
{

// Fixed storage so readers never race with registration growing a container.
struct Registry
{
    std::mutex mutex;
    std::atomic<std::uint32_t> count{0};
    std::array<ComponentInfo, kMaxComponentTypes> infos;
};

Registry& GetRegistry()
{
    static Registry registry;
    return registry;
}

} // namespace

ComponentTypeId RegisterComponent(ComponentInfo info)
{
    Registry& registry = GetRegistry();
    const std::scoped_lock lock(registry.mutex);
    const std::uint32_t id = registry.count.load(std::memory_order_relaxed);
    INTERI_VERIFY(id < kMaxComponentTypes, "more than kMaxComponentTypes component types");
    registry.infos[id] = std::move(info);
    registry.count.store(id + 1, std::memory_order_release);
    return id;
}

const ComponentInfo& ComponentInfoOf(ComponentTypeId id) noexcept
{
    Registry& registry = GetRegistry();
    INTERI_ASSERT(id < registry.count.load(std::memory_order_acquire), "unregistered component id");
    return registry.infos[id];
}

} // namespace interi
