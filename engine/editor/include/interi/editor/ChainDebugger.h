// SPDX-License-Identifier: MIT
#pragma once

// The chain editor's live debugger (22.6), built on the executor's trace (ExecutorDesc::trace). After each step of a
// play session the editor passes the trace to Observe, which:
//   - records which nodes ran and which links were evaluated, so the canvas can light them (Heat);
//   - keeps an event timeline;
//   - checks the breakpoints. A breakpoint on a node hits when the node fires or runs; one on a link when its
//     condition is evaluated. Breakpoints work at step granularity: the step finishes, then the session pauses
//     (ScenePlaySession::SetStepObserver), and Step runs the next one. A pause never happens mid-wave, so the
//     executor and its determinism are untouched.
// Only records of the chain whose asset is the debugger's target count (all chains while no target is set).

#include <interi/chain/Executor.h>
#include <interi/values/Value.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace interi::scene
{
class SceneRuntime;
} // namespace interi::scene

namespace interi::editor
{

/// One line of the event timeline.
struct TimelineEntry
{
    std::uint64_t step = 0;
    chain::TraceRecord::Kind kind = chain::TraceRecord::Kind::Fired;
    std::string node;
    std::string detail;  ///< Output name, link index, or the message.
    bool passed = false; ///< Condition results.
    std::string payload; ///< The payload formatted: "damage=20, target=1".
};

struct WatchValue
{
    std::string id;
    std::optional<values::Value> value; ///< nullopt: no such Value.
    std::string text;
};

class ChainDebugger
{
public:
    /// How many steps a node or link stays lit after it ran.
    static constexpr std::uint64_t kHeatSteps = 30;
    static constexpr std::size_t kTimelineCapacity = 512;

    /// The chain asset being debugged (a runtime chain path); empty: every chain.
    void SetTarget(std::string path);
    [[nodiscard]] const std::string& Target() const noexcept { return m_target; }

    // Breakpoints (by node id and asset link index). Toggling returns whether it is set afterwards.
    bool ToggleNodeBreakpoint(std::string_view node);
    bool ToggleLinkBreakpoint(std::size_t link);
    [[nodiscard]] bool HasNodeBreakpoint(std::string_view node) const
    {
        return m_nodeBreakpoints.contains(std::string(node));
    }
    [[nodiscard]] bool HasLinkBreakpoint(std::size_t link) const { return m_linkBreakpoints.contains(link); }
    void ClearBreakpoints();

    /// Reads the last step's trace. Returns what hit a breakpoint (the first one, in trace order), if anything.
    std::optional<std::string> Observe(const scene::SceneRuntime& runtime);
    /// The same on explicit records: `chainPath(chain)` names a record's chain asset.
    template <typename PathOf>
    std::optional<std::string> ObserveRecords(const std::vector<chain::TraceRecord>& trace, const PathOf& chainPath)
    {
        std::optional<std::string> hit;
        for (const chain::TraceRecord& record : trace)
        {
            if (!m_target.empty() && chainPath(record.chain) != m_target)
            {
                continue;
            }
            if (auto text = Record(record); text && !hit)
            {
                hit = std::move(text);
            }
        }
        return hit;
    }

    /// 1 when the node/link just ran, fading to 0 after kHeatSteps steps; 0 if it never did.
    [[nodiscard]] float NodeHeat(std::string_view node, std::uint64_t currentStep) const;
    [[nodiscard]] float LinkHeat(std::size_t link, std::uint64_t currentStep) const;
    /// Whether the link's last evaluated condition passed.
    [[nodiscard]] bool LinkPassed(std::size_t link) const;

    [[nodiscard]] const std::deque<TimelineEntry>& Timeline() const noexcept { return m_timeline; }
    /// The last breakpoint hit's description (empty before one).
    [[nodiscard]] const std::string& LastHit() const noexcept { return m_lastHit; }

    // Watch panel.
    void AddWatch(std::string valueId);
    void RemoveWatch(std::string_view valueId);
    [[nodiscard]] const std::vector<std::string>& Watches() const noexcept { return m_watches; }
    [[nodiscard]] std::vector<WatchValue> ReadWatches(const chain::IValueReader& values) const;

    /// Forgets heat, timeline and the last hit (a new play session); breakpoints and watches stay.
    void Reset();

private:
    std::optional<std::string> Record(const chain::TraceRecord& record);

    std::string m_target;
    std::set<std::string> m_nodeBreakpoints;
    std::set<std::size_t> m_linkBreakpoints;
    std::map<std::string, std::uint64_t> m_nodeStep;
    std::map<std::size_t, std::uint64_t> m_linkStep;
    std::map<std::size_t, bool> m_linkPassed;
    std::deque<TimelineEntry> m_timeline;
    std::vector<std::string> m_watches;
    std::string m_lastHit;
};

} // namespace interi::editor
