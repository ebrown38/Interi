// SPDX-License-Identifier: MIT
#pragma once

#include <interi/jobs/JobSystem.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace interi
{

/// A DAG of tasks that is built once (e.g. per frame, or once for a fixed pipeline) and run any number of times on
/// a JobSystem. A node starts when all its predecessors have finished; independent nodes run in parallel.
///
/// Building (AddNode/AddDependency/Clear) is single-threaded and must not overlap Run. When asserts are enabled,
/// Run checks for cycles first; a cyclic graph asserts and runs nothing.
class TaskGraph
{
public:
    using NodeId = std::uint32_t;

    TaskGraph() = default;
    TaskGraph(const TaskGraph&) = delete;
    TaskGraph& operator=(const TaskGraph&) = delete;
    TaskGraph(TaskGraph&&) = default;
    TaskGraph& operator=(TaskGraph&&) = default;
    ~TaskGraph() = default;

    /// Adds a task. `function` is invoked once per Run, so it is kept (not consumed).
    template <typename F>
    NodeId AddNode(F&& function, JobPriority priority = JobPriority::Normal)
    {
        m_nodes.push_back(Node{std::move_only_function<void()>(std::forward<F>(function)), {}, 0, priority});
        return static_cast<NodeId>(m_nodes.size() - 1);
    }

    /// `after` starts only once `before` has finished.
    void AddDependency(NodeId before, NodeId after);

    /// Runs every node respecting dependencies and returns when all have finished. The calling thread helps.
    void Run(JobSystem& jobs);

    void Clear() noexcept;
    [[nodiscard]] std::size_t NodeCount() const noexcept { return m_nodes.size(); }

private:
    struct Node
    {
        std::move_only_function<void()> function;
        std::vector<NodeId> successors;
        std::uint32_t predecessorCount;
        JobPriority priority;
    };

    void SubmitNode(JobSystem& jobs, JobCounter& counter, NodeId node);
    [[nodiscard]] bool HasCycle() const;

    std::vector<Node> m_nodes;
    std::unique_ptr<std::atomic<std::uint32_t>[]> m_remaining; ///< Unfinished predecessors, per node, during Run.
    std::size_t m_remainingSize = 0;
};

} // namespace interi
