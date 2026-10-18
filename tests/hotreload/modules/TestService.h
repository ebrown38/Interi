// SPDX-License-Identifier: MIT
#pragma once

// A host service for the hot-reload tests: modules register callbacks with it, the host takes them back.
#include <interi/hotreload/ModuleService.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace interi::testing
{

class TestService final : public hotreload::IModuleService
{
public:
    static constexpr std::string_view kServiceType = "interi.test.TestService";
    [[nodiscard]] std::string_view ServiceType() const noexcept override { return kServiceType; }

    struct Entry
    {
        std::string provider;
        std::function<int()> callback;
    };
    using Entries = std::vector<Entry>;

    void Add(std::string provider, std::function<int()> callback)
    {
        m_entries.push_back({std::move(provider), std::move(callback)});
    }

    [[nodiscard]] std::shared_ptr<void> Extract(std::string_view provider) override
    {
        auto taken = std::make_shared<Entries>();
        for (Entry& entry : m_entries)
        {
            if (entry.provider == provider)
            {
                taken->push_back(std::move(entry));
            }
        }
        std::erase_if(m_entries, [&](const Entry& entry) { return !entry.callback; });
        return taken;
    }

    void Restore(std::shared_ptr<void> token) override
    {
        for (Entry& entry : *std::static_pointer_cast<Entries>(token))
        {
            m_entries.push_back(std::move(entry));
        }
    }

    [[nodiscard]] std::vector<int> Call() const
    {
        std::vector<int> results;
        for (const Entry& entry : m_entries)
        {
            results.push_back(entry.callback());
        }
        return results;
    }
    [[nodiscard]] std::size_t Count() const noexcept { return m_entries.size(); }

private:
    Entries m_entries;
};

} // namespace interi::testing
