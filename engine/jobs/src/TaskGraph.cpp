// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Profiling.h>
#include <interi/jobs/TaskGraph.h>

namespace interi
{

void TaskGraph::AddDependency(NodeId before, NodeId after)
{
    INTERI_ASSERT(before < m_nodes.size() && after < m_nodes.size(), "TaskGraph::AddDependency: unknown node");
    m_nodes[before].successors.push_back(after);
    ++m_nodes[after].predecessorCount;
}

void TaskGraph::Clear() noexcept
{
    m_nodes.clear();
}

bool TaskGraph::HasCycle() const
{
    // Kahn's algorithm: a DAG can be fully consumed by repeatedly removing nodes without predecessors.
    std::vector<std::uint32_t> remaining(m_nodes.size());
    std::vector<NodeId> ready;
    for (NodeId i = 0; i < m_nodes.size(); ++i)
    {
        remaining[i] = m_nodes[i].predecessorCount;
        if (remaining[i] == 0)
        {
            ready.push_back(i);
        }
    }
    std::size_t visited = 0;
    while (!ready.empty())
    {
        const NodeId node = ready.back();
        ready.pop_back();
        ++visited;
        for (const NodeId successor : m_nodes[node].successors)
        {
            if (--remaining[successor] == 0)
            {
                ready.push_back(successor);
            }
        }
    }
    return visited != m_nodes.size();
}

void TaskGraph::Run(JobSystem& jobs)
{
    INTERI_ZONE();
    if constexpr (kAssertsEnabled)
    {
        if (HasCycle())
        {
            INTERI_ASSERT(false, "TaskGraph contains a cycle");
            return;
        }
    }
    if (m_remainingSize != m_nodes.size())
    {
        m_remaining = std::make_unique<std::atomic<std::uint32_t>[]>(m_nodes.size());
        m_remainingSize = m_nodes.size();
    }
    for (std::size_t i = 0; i < m_nodes.size(); ++i)
    {
        m_remaining[i].store(m_nodes[i].predecessorCount, std::memory_order_relaxed);
    }

    JobCounter counter;
    for (NodeId i = 0; i < m_nodes.size(); ++i)
    {
        if (m_nodes[i].predecessorCount == 0)
        {
            SubmitNode(jobs, counter, i);
        }
    }
    jobs.WaitFor(counter);
}

void TaskGraph::SubmitNode(JobSystem& jobs, JobCounter& counter, NodeId node)
{
    jobs.Submit(
        [this, &jobs, &counter, node]
        {
            m_nodes[node].function();
            for (const NodeId successor : m_nodes[node].successors)
            {
                // acq_rel: the last finishing predecessor sees every other predecessor's writes before it submits.
                if (m_remaining[successor].fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    SubmitNode(jobs, counter, successor); // counted before this job finishes, so no early wake-up
                }
            }
        },
        &counter, m_nodes[node].priority);
}

} // namespace interi
