// SPDX-License-Identifier: MIT
#pragma once

#include <interi/platform/InputEvents.h>
#include <interi/platform/Window.h>

#include <gmock/gmock.h>

#include <utility>
#include <vector>

namespace interi::testing
{

/// An IEventSource fed by the test: Push events, then the next PollEvents delivers them.
class FakeEventSource final : public IEventSource
{
public:
    void Push(InputEvent event) { m_queued.push_back(event); }

    void PollEvents(std::vector<InputEvent>& events) override
    {
        events.insert(events.end(), m_queued.begin(), m_queued.end());
        m_queued.clear();
    }

private:
    std::vector<InputEvent> m_queued;
};

class MockWindow : public IWindow
{
public:
    MOCK_METHOD(WindowExtent, Size, (), (const, override));
    MOCK_METHOD(void, SetTitle, (std::string_view title), (override));
    MOCK_METHOD(void*, NativeHandle, (), (const, override));
};

} // namespace interi::testing
