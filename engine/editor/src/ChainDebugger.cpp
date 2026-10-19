// SPDX-License-Identifier: MIT
#include <interi/editor/ChainDebugger.h>
#include <interi/scene/SceneRuntime.h>

#include <algorithm>
#include <format>

namespace interi::editor
{

namespace
{

float Fade(std::uint64_t ranAt, std::uint64_t now)
{
    if (now < ranAt)
    {
        return 1.0f;
    }
    const std::uint64_t age = now - ranAt;
    return age >= ChainDebugger::kHeatSteps
               ? 0.0f
               : 1.0f - static_cast<float>(age) / static_cast<float>(ChainDebugger::kHeatSteps);
}

std::string PayloadText(const std::vector<values::Value>& payload)
{
    std::string text;
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        text += i == 0 ? "" : ", ";
        text += values::FormatValue(payload[i]);
    }
    return text;
}

} // namespace

void ChainDebugger::SetTarget(std::string path)
{
    m_target = std::move(path);
}

bool ChainDebugger::ToggleNodeBreakpoint(std::string_view node)
{
    const std::string key(node);
    if (m_nodeBreakpoints.erase(key) > 0)
    {
        return false;
    }
    m_nodeBreakpoints.insert(key);
    return true;
}

bool ChainDebugger::ToggleLinkBreakpoint(std::size_t link)
{
    if (m_linkBreakpoints.erase(link) > 0)
    {
        return false;
    }
    m_linkBreakpoints.insert(link);
    return true;
}

void ChainDebugger::ClearBreakpoints()
{
    m_nodeBreakpoints.clear();
    m_linkBreakpoints.clear();
}

std::optional<std::string> ChainDebugger::Record(const chain::TraceRecord& record)
{
    using Kind = chain::TraceRecord::Kind;
    TimelineEntry entry;
    entry.step = record.step;
    entry.kind = record.kind;
    entry.node = record.node;
    entry.passed = record.passed;
    entry.payload = PayloadText(record.payload);
    std::optional<std::string> hit;
    switch (record.kind)
    {
    case Kind::Fired:
        entry.detail = record.output;
        m_nodeStep[record.node] = record.step;
        if (HasNodeBreakpoint(record.node))
        {
            hit = std::format("step {}: {} fired {}", record.step, record.node, record.output);
        }
        break;
    case Kind::Ran:
        m_nodeStep[record.node] = record.step;
        if (HasNodeBreakpoint(record.node))
        {
            hit = std::format("step {}: {} ran", record.step, record.node);
        }
        break;
    case Kind::Condition:
        entry.detail = std::format("link {}", record.link);
        m_linkStep[record.link] = record.step;
        m_linkPassed[record.link] = record.passed;
        if (HasLinkBreakpoint(record.link))
        {
            hit = std::format("step {}: link {} condition {}", record.step, record.link,
                              record.passed ? "passed" : "failed");
        }
        break;
    case Kind::AuthoritySkip:
        break;
    case Kind::Dropped:
    case Kind::WriteRejected:
        entry.detail = record.message;
        break;
    }
    m_timeline.push_back(std::move(entry));
    while (m_timeline.size() > kTimelineCapacity)
    {
        m_timeline.pop_front();
    }
    if (hit)
    {
        m_lastHit = *hit;
    }
    return hit;
}

std::optional<std::string> ChainDebugger::Observe(const scene::SceneRuntime& runtime)
{
    return ObserveRecords(runtime.Executor().Trace(), [&](std::uint32_t chain) { return runtime.ChainPath(chain); });
}

float ChainDebugger::NodeHeat(std::string_view node, std::uint64_t currentStep) const
{
    const auto found = m_nodeStep.find(std::string(node));
    return found == m_nodeStep.end() ? 0.0f : Fade(found->second, currentStep);
}

float ChainDebugger::LinkHeat(std::size_t link, std::uint64_t currentStep) const
{
    const auto found = m_linkStep.find(link);
    return found == m_linkStep.end() ? 0.0f : Fade(found->second, currentStep);
}

bool ChainDebugger::LinkPassed(std::size_t link) const
{
    const auto found = m_linkPassed.find(link);
    return found != m_linkPassed.end() && found->second;
}

void ChainDebugger::AddWatch(std::string valueId)
{
    if (!valueId.empty() && !std::ranges::contains(m_watches, valueId))
    {
        m_watches.push_back(std::move(valueId));
    }
}

void ChainDebugger::RemoveWatch(std::string_view valueId)
{
    std::erase(m_watches, std::string(valueId));
}

std::vector<WatchValue> ChainDebugger::ReadWatches(const chain::IValueReader& values) const
{
    std::vector<WatchValue> result;
    for (const std::string& id : m_watches)
    {
        WatchValue watch;
        watch.id = id;
        if (const values::Value* value = values.Read(id))
        {
            watch.value = *value;
            watch.text = values::FormatValue(*value);
        }
        else
        {
            watch.text = "(undefined)";
        }
        result.push_back(std::move(watch));
    }
    return result;
}

void ChainDebugger::Reset()
{
    m_nodeStep.clear();
    m_linkStep.clear();
    m_linkPassed.clear();
    m_timeline.clear();
    m_lastHit.clear();
}

} // namespace interi::editor
