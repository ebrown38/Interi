// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/ecs/Component.h>

#include <array>
#include <atomic>
#include <mutex>

namespace interi
{

// Fixed storage so readers never race with registration growing a container. Entries are only appended.
class ComponentRegistry
{
public:
    std::mutex mutex;
    std::atomic<std::uint32_t> count{0};
    std::array<ComponentInfo, kMaxComponentTypes> infos;
};

namespace
{

ComponentRegistry& OwnRegistry()
{
    static ComponentRegistry registry;
    return registry;
}

// Set in a hot-reloaded module's copy of interi_ecs: the host's registry, and module mode.
std::atomic<ComponentRegistry*> g_hostRegistry{nullptr};

bool SameLayout(const ComponentInfo& a, const ComponentInfo& b) noexcept
{
    return a.size == b.size && a.alignment == b.alignment && a.version == b.version;
}

} // namespace

bool IsAnonymousComponentName(std::string_view name) noexcept
{
    return name.find("(anonymous namespace)") != std::string_view::npos ||
           name.find("`anonymous namespace'") != std::string_view::npos;
}

ComponentRegistry& ActiveComponentRegistry() noexcept
{
    ComponentRegistry* host = g_hostRegistry.load(std::memory_order_acquire);
    return host != nullptr ? *host : OwnRegistry();
}

void AttachToHostComponentRegistry(ComponentRegistry& host) noexcept
{
    g_hostRegistry.store(&host, std::memory_order_release);
}

void DetachFromHostComponentRegistry() noexcept
{
    g_hostRegistry.store(nullptr, std::memory_order_release);
}

ComponentTypeId RegisterComponent(ComponentInfo info)
{
    const bool moduleMode = g_hostRegistry.load(std::memory_order_acquire) != nullptr;
    ComponentRegistry& registry = ActiveComponentRegistry();
    const std::scoped_lock lock(registry.mutex);
    const std::uint32_t count = registry.count.load(std::memory_order_relaxed);
    if (!IsAnonymousComponentName(info.name))
    {
        for (std::uint32_t id = 0; id < count; ++id)
        {
            const ComponentInfo& existing = registry.infos[id];
            if (existing.name == info.name && SameLayout(existing, info))
            {
                return id;
            }
        }
    }
    INTERI_VERIFY(!moduleMode || (info.relocate == nullptr && info.destroy == nullptr),
                  "a hot-reloaded module may only add trivially copyable component types");
    INTERI_VERIFY(count < kMaxComponentTypes, "more than kMaxComponentTypes component types");
    registry.infos[count] = std::move(info);
    registry.count.store(count + 1, std::memory_order_release);
    return count;
}

const ComponentInfo& ComponentInfoOf(ComponentTypeId id) noexcept
{
    ComponentRegistry& registry = ActiveComponentRegistry();
    INTERI_ASSERT(id < registry.count.load(std::memory_order_acquire), "unregistered component id");
    return registry.infos[id];
}

std::uint32_t ComponentTypeCount() noexcept
{
    return ActiveComponentRegistry().count.load(std::memory_order_acquire);
}

} // namespace interi
