// SPDX-License-Identifier: MIT
// Many jobs record contact events at once into the per-thread buffers; the merge must keep every one of them.
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/physics/ContactEvents.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using interi::JobSystem;
using interi::JobSystemDesc;
using interi::physics::ContactCollector;
using interi::physics::ContactEvent;

TEST(ContactEvents, ConcurrentWrites_NoLoss)
{
    constexpr std::size_t kEvents = 200'000;
    JobSystemDesc desc;
    desc.workerCount = 8;
    JobSystem jobs(desc);
    ContactCollector collector(jobs, 0.0f);
    std::vector<ContactEvent> merged;

    for (int round = 0; round < 3; ++round)
    {
        interi::ParallelFor(jobs, 0, kEvents, 64,
                            [&](std::size_t i)
                            {
                                ContactEvent event;
                                event.bodyA = JPH::BodyID(static_cast<std::uint32_t>(i % 1000));
                                event.bodyB = JPH::BodyID(static_cast<std::uint32_t>(1000 + i / 1000));
                                event.impulse = static_cast<float>(i);
                                collector.Record(event);
                            });
        collector.Merge(merged);

        ASSERT_EQ(merged.size(), kEvents) << "round " << round;
        // Every event exactly once: the impulses are 0..kEvents-1 in some order.
        std::vector<bool> seen(kEvents, false);
        for (const ContactEvent& event : merged)
        {
            const auto i = static_cast<std::size_t>(event.impulse);
            ASSERT_LT(i, kEvents);
            ASSERT_FALSE(seen[i]) << "duplicate " << i;
            seen[i] = true;
        }
        // Sorted by body pair.
        for (std::size_t i = 1; i < merged.size(); ++i)
        {
            ASSERT_LE(merged[i - 1].bodyA.GetIndexAndSequenceNumber(), merged[i].bodyA.GetIndexAndSequenceNumber());
        }
    }
}
