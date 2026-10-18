// SPDX-License-Identifier: MIT
#include <interi/runtime/TriggerVolumes.h>

#include <gtest/gtest.h>

using namespace interi;

namespace
{

TriggerVolume Box(std::uint64_t id, Vec3 center, Vec3 half)
{
    return {.id = id, .shape = VolumeShape::Box, .center = center, .halfExtents = half, .radius = 0.0f, .mask = ~0u};
}

TriggerVolume Sphere(std::uint64_t id, Vec3 center, float radius)
{
    return {.id = id, .shape = VolumeShape::Sphere, .center = center, .halfExtents = {}, .radius = radius, .mask = ~0u};
}

TriggerActor Actor(std::uint64_t id, Vec3 position, float radius = 0.0f, std::uint32_t layers = 1u)
{
    return {.id = id, .position = position, .radius = radius, .layers = layers};
}

} // namespace

TEST(TriggerVolumes, BoxContainsPoint_IncludingFace)
{
    const TriggerVolume box = Box(1, {0, 0, 0}, {1, 1, 1});
    EXPECT_TRUE(Overlaps(box, Actor(1, {0.5f, -0.5f, 0.9f})));
    EXPECT_TRUE(Overlaps(box, Actor(1, {1.0f, 0.0f, 0.0f})));
    EXPECT_FALSE(Overlaps(box, Actor(1, {1.01f, 0.0f, 0.0f})));
}

TEST(TriggerVolumes, BoxVsActorRadius_UsesDistanceToBox)
{
    const TriggerVolume box = Box(1, {0, 0, 0}, {1, 1, 1});
    EXPECT_TRUE(Overlaps(box, Actor(1, {1.4f, 0, 0}, 0.5f)));
    EXPECT_FALSE(Overlaps(box, Actor(1, {1.6f, 0, 0}, 0.5f)));
    // Diagonal: the corner is at distance sqrt(0.3^2 * 3) ~ 0.52 from (1.3, 1.3, 1.3).
    EXPECT_FALSE(Overlaps(box, Actor(1, {1.3f, 1.3f, 1.3f}, 0.5f)));
    EXPECT_TRUE(Overlaps(box, Actor(1, {1.3f, 1.3f, 1.3f}, 0.53f)));
}

TEST(TriggerVolumes, Sphere_AddsActorRadius)
{
    const TriggerVolume sphere = Sphere(1, {0, 0, 0}, 2.0f);
    EXPECT_TRUE(Overlaps(sphere, Actor(1, {2.0f, 0, 0})));
    EXPECT_FALSE(Overlaps(sphere, Actor(1, {2.5f, 0, 0})));
    EXPECT_TRUE(Overlaps(sphere, Actor(1, {2.5f, 0, 0}, 0.5f)));
}

TEST(TriggerVolumes, MaskFiltersLayers)
{
    TriggerVolume box = Box(1, {0, 0, 0}, {1, 1, 1});
    box.mask = 0b10;
    EXPECT_FALSE(Overlaps(box, Actor(1, {0, 0, 0}, 0.0f, 0b01)));
    EXPECT_TRUE(Overlaps(box, Actor(1, {0, 0, 0}, 0.0f, 0b11)));
}

TEST(OverlapTracker, ReportsOnlyChanges)
{
    OverlapTracker tracker;
    const std::vector<TriggerVolume> volumes = {Box(7, {0, 0, 0}, {1, 1, 1})};
    auto events = tracker.Update(volumes, {Actor(1, {5, 0, 0})});
    EXPECT_TRUE(events.empty());
    events = tracker.Update(volumes, {Actor(1, {0, 0, 0})});
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], (OverlapEvent{7, 1, true}));
    EXPECT_TRUE(tracker.IsOverlapping(7, 1));
    EXPECT_TRUE(tracker.Update(volumes, {Actor(1, {0.5f, 0, 0})}).empty()); // still inside: no event
    events = tracker.Update(volumes, {Actor(1, {5, 0, 0})});
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], (OverlapEvent{7, 1, false}));
    EXPECT_EQ(tracker.ActiveCount(), 0u);
}

TEST(OverlapTracker, RemovedActorOrVolume_EndsItsPairs)
{
    OverlapTracker tracker;
    const std::vector<TriggerVolume> volumes = {Box(1, {0, 0, 0}, {1, 1, 1})};
    (void)tracker.Update(volumes, {Actor(9, {0, 0, 0})});
    auto events = tracker.Update(volumes, {});
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].begin);
    (void)tracker.Update(volumes, {Actor(9, {0, 0, 0})});
    events = tracker.Update({}, {Actor(9, {0, 0, 0})});
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].begin);
}

TEST(OverlapTracker, Order_IsByVolumeThenActor_EndsFirst_IndependentOfInputOrder)
{
    const std::vector<TriggerVolume> volumes = {Sphere(5, {0, 0, 0}, 10.0f), Sphere(2, {0, 0, 0}, 10.0f)};
    const std::vector<TriggerActor> actors = {Actor(8, {0, 0, 0}), Actor(3, {0, 0, 0})};
    OverlapTracker a;
    const auto events = a.Update(volumes, actors);
    const std::vector<OverlapEvent> expected = {{2, 3, true}, {2, 8, true}, {5, 3, true}, {5, 8, true}};
    EXPECT_EQ(events, expected);

    OverlapTracker b;
    const std::vector<TriggerVolume> reversedVolumes = {volumes[1], volumes[0]};
    const std::vector<TriggerActor> reversedActors = {actors[1], actors[0]};
    EXPECT_EQ(b.Update(reversedVolumes, reversedActors), expected);
}

TEST(OverlapTracker, Clear_ForgetsPairs)
{
    OverlapTracker tracker;
    const std::vector<TriggerVolume> volumes = {Box(1, {0, 0, 0}, {1, 1, 1})};
    (void)tracker.Update(volumes, {Actor(2, {0, 0, 0})});
    tracker.Clear();
    EXPECT_EQ(tracker.Update(volumes, {Actor(2, {0, 0, 0})}).size(), 1u);
}
