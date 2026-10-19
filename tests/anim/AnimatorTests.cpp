// SPDX-License-Identifier: MIT
#include <interi/anim/AnimEvents.h>
#include <interi/chain/Executor.h>
#include <interi/core/math/Transform.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform/FileSystem.h>

#include "../chain/ChainTestUtil.h"
#include "AnimTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::anim;
using namespace interi::anim::testing;

namespace
{

AnimationLibrary MakeLibrary()
{
    AnimationLibrary library;
    EXPECT_TRUE(library.Add(ArmSet()).has_value());
    EXPECT_TRUE(library.BindArchetype("com.test:Arm", "arm").has_value());
    return library;
}

} // namespace

TEST(AnimGraph, Json_RoundTrips)
{
    const AnimGraph graph = ArmSet().graph;
    auto parsed = ParseAnimGraph(WriteAnimGraph(graph));
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, graph);
}

TEST(AnimGraph, Parse_Malformed_Rejected)
{
    EXPECT_FALSE(ParseAnimGraph("[]").has_value());
    EXPECT_FALSE(ParseAnimGraph(R"({"version":1,"states":[{"id":"a"}]})").has_value());
    EXPECT_FALSE(ParseAnimGraph(R"({"version":1,"transitions":[{"from":"a","to":"b"}]})").has_value());
    EXPECT_EQ(ParseAnimGraph(R"({"version":2})").error().code, ErrorCode::VersionMismatch);
}

TEST(AnimationLibrary, Add_InvalidSets_Rejected)
{
    AnimationLibrary library;
    AnimationSet unknownClip = ArmSet();
    unknownClip.graph.states[0].clip = "missing";
    EXPECT_FALSE(library.Add(unknownClip).has_value());
    AnimationSet badDefault = ArmSet();
    badDefault.graph.defaultState = "nope";
    EXPECT_FALSE(library.Add(badDefault).has_value());
    AnimationSet badTransition = ArmSet();
    badTransition.graph.transitions[0].to = "nope";
    EXPECT_FALSE(library.Add(badTransition).has_value());
    EXPECT_TRUE(library.Add(ArmSet()).has_value());
    EXPECT_FALSE(library.Add(ArmSet()).has_value()); // repeated id
}

TEST(AnimationLibrary, MakeAnimator_UnboundArchetype_NotFound)
{
    const AnimationLibrary library = MakeLibrary();
    EXPECT_TRUE(library.MakeAnimator("com.test:Arm").has_value());
    const auto missing = library.MakeAnimator("com.test:Ghost");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(MakeLibrary().BindArchetype("x", "nope").has_value());
}

TEST(Animator, Trigger_CrossfadesThenPlaysTarget)
{
    const AnimationLibrary library = MakeLibrary();
    AnimatorComponent animator = *library.MakeAnimator("com.test:Arm");
    Pose pose;
    StepAnimator(library, animator, 0.1f, pose);
    EXPECT_EQ(pose[1].position, Vec3(0.0f, 1.0f, 0.0f)); // idle holds the bind pose

    animator.requestTrigger = AnimNameHash("go");
    StepAnimator(library, animator, 0.25f, pose);
    EXPECT_EQ(animator.next, AnimNameHash("raise"));
    // Halfway through a 0.5 s fade between the bind pose (y = 1) and raise at t = 0.25 (y = 1.5).
    EXPECT_NEAR(pose[1].position.y, 1.25f, 1e-5f);

    StepAnimator(library, animator, 0.25f, pose);
    EXPECT_EQ(animator.next, 0u);
    EXPECT_EQ(animator.state, AnimNameHash("raise"));
    EXPECT_NEAR(pose[1].position.y, 2.0f, 1e-5f);

    StepAnimator(library, animator, 10.0f, pose); // one-shot stays on its last frame
    EXPECT_EQ(pose[1].position, Vec3(0.0f, 3.0f, 0.0f));
    EXPECT_LE(animator.time, 1.0f);
}

TEST(Animator, AnyStateTransition_ZeroDurationSwitchesAtOnce)
{
    const AnimationLibrary library = MakeLibrary();
    AnimatorComponent animator = *library.MakeAnimator("com.test:Arm");
    animator.state = AnimNameHash("raise");
    animator.requestTrigger = AnimNameHash("stop");
    Pose pose;
    StepAnimator(library, animator, 0.0f, pose);
    EXPECT_EQ(animator.state, AnimNameHash("idle"));
    EXPECT_EQ(animator.next, 0u);
}

TEST(Animator, UnknownRequests_AreIgnoredAndConsumed)
{
    const AnimationLibrary library = MakeLibrary();
    AnimatorComponent animator = *library.MakeAnimator("com.test:Arm");
    animator.requestTrigger = AnimNameHash("nothing");
    animator.requestState = AnimNameHash("nowhere");
    Pose pose;
    StepAnimator(library, animator, 0.1f, pose);
    EXPECT_EQ(animator.requestTrigger, 0u);
    EXPECT_EQ(animator.requestState, 0u);
    EXPECT_EQ(animator.next, 0u);
}

TEST(Animator, Play_RequestWhileFading_SnapsThenFades)
{
    const AnimationLibrary library = MakeLibrary();
    AnimatorComponent animator = *library.MakeAnimator("com.test:Arm");
    animator.requestTrigger = AnimNameHash("go");
    Pose pose;
    StepAnimator(library, animator, 0.1f, pose);
    ASSERT_EQ(animator.next, AnimNameHash("raise"));
    animator.requestState = AnimNameHash("idle");
    animator.requestBlend = 0.2f;
    StepAnimator(library, animator, 0.05f, pose);
    EXPECT_EQ(animator.state, AnimNameHash("raise"));
    EXPECT_EQ(animator.next, AnimNameHash("idle"));
}

TEST(Animator, SameInputs_SameBits)
{
    const AnimationLibrary library = MakeLibrary();
    const auto run = [&]
    {
        AnimatorComponent animator = *library.MakeAnimator("com.test:Arm");
        Pose pose;
        animator.requestTrigger = AnimNameHash("go");
        for (int i = 0; i < 40; ++i)
        {
            StepAnimator(library, animator, 1.0f / 60.0f, pose);
        }
        return pose[1].position.y;
    };
    EXPECT_EQ(run(), run());
}

namespace
{

struct EventFixture
{
    chain::testing::TestProject project;
    World world;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    CommandBuffers commands{jobs};
    std::unique_ptr<chain::ChainExecutor> executor;

    EventFixture()
    {
        for (std::string& id : AnimationContracts())
        {
            project.modules.DeclareContract(std::move(id));
        }
        EXPECT_TRUE(RegisterAnimationLibrary(*project.chains).has_value());
        executor = std::make_unique<chain::ChainExecutor>(*project.chains, project.values.get(), chain::ExecutorDesc{});
    }

    Entity Spawn(std::uint64_t id, const AnimatorComponent& animator)
    {
        return world.CreateEntity(chain::StableIdComponent{.id = id}, Transform{}, animator);
    }

    void Run(const std::string& event, std::uint64_t entity)
    {
        const auto chain = executor->AddChain(
            chain::testing::Chain(R"json({"version": 1, "nodes": [{"id": "start", "event": "trigger.Start"},
                {"id": "a", )json" +
                                  event + R"json(}], "links": [{"from": "start", "to": "a"}]})json"),
            entity);
        ASSERT_TRUE(executor->IsActive(chain));
        executor->Step(world, jobs, commands);
    }
};

} // namespace

TEST(AnimEvents, Trigger_SetsRequestOnEntityAnimator)
{
    EventFixture f;
    const AnimationLibrary library = MakeLibrary();
    const Entity arm = f.Spawn(4, *library.MakeAnimator("com.test:Arm"));
    f.Run(R"json("event": "anim.Trigger", "params": {"trigger": {"const": "go"}})json", 4);
    EXPECT_EQ(f.world.Get<AnimatorComponent>(arm)->requestTrigger, AnimNameHash("go"));
}

TEST(AnimEvents, Play_TargetsOtherEntityAndCarriesBlend)
{
    EventFixture f;
    const AnimationLibrary library = MakeLibrary();
    const Entity self = f.Spawn(1, *library.MakeAnimator("com.test:Arm"));
    const Entity other = f.Spawn(2, *library.MakeAnimator("com.test:Arm"));
    f.Run(R"json("event": "anim.Play", "params": {"target": {"const": 2}, "state": {"const": "raise"},
        "blend": {"const": 0.3}})json",
          1);
    EXPECT_EQ(f.world.Get<AnimatorComponent>(self)->requestState, 0u);
    EXPECT_EQ(f.world.Get<AnimatorComponent>(other)->requestState, AnimNameHash("raise"));
    EXPECT_NEAR(f.world.Get<AnimatorComponent>(other)->requestBlend, 0.3f, 1e-6f);
}

TEST(AnimEvents, EntityWithoutAnimator_IsSkipped)
{
    EventFixture f;
    const Entity plain = f.world.CreateEntity(chain::StableIdComponent{.id = 9}, Transform{});
    f.Run(R"json("event": "anim.Trigger", "params": {"trigger": {"const": "go"}})json", 9);
    EXPECT_FALSE(f.world.Has<AnimatorComponent>(plain));
}

TEST(LoadModuleAnimations, LoadsFilesAndBindsArchetypes)
{
    const AnimationSet source = ArmSet();
    MemoryFileSystem files;
    const auto put = [&](const std::string& path, const std::string& text)
    { ASSERT_TRUE(files.WriteFile(path, std::as_bytes(std::span(text))).has_value()); };
    put("anim/arm.skeleton.json", WriteSkeleton(source.skeleton));
    put("anim/raise.clip.json", WriteClip(source.clips[0], source.skeleton));
    put("anim/hold.clip.json", WriteClip(source.clips[1], source.skeleton));
    put("anim/arm.animstates.json", WriteAnimGraph(source.graph));
    auto manifest = sdk::ParseManifest(R"({"id": "com.test.arm", "version": "1.0.0", "animationSets": [
        {"name": "arm", "skeleton": "anim/arm.skeleton.json",
         "clips": ["anim/raise.clip.json", "anim/hold.clip.json"], "graph": "anim/arm.animstates.json",
         "archetypes": ["com.test.arm:Arm"]}]})");
    ASSERT_TRUE(manifest.has_value()) << manifest.error().message;

    AnimationLibrary library;
    const auto loaded = LoadModuleAnimations(*manifest, files, library);
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_NE(library.Find("com.test.arm:arm"), nullptr);
    EXPECT_TRUE(library.MakeAnimator("com.test.arm:Arm").has_value());

    AnimationLibrary missing;
    MemoryFileSystem empty;
    EXPECT_FALSE(LoadModuleAnimations(*manifest, empty, missing).has_value());
}
