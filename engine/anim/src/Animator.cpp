// SPDX-License-Identifier: MIT
#include <interi/anim/Animator.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace interi::anim
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

const AnimState* FindState(const AnimGraph& graph, std::uint32_t hash) noexcept
{
    if (hash == 0)
    {
        hash = AnimNameHash(graph.defaultState);
    }
    for (const AnimState& state : graph.states)
    {
        if (AnimNameHash(state.id) == hash)
        {
            return &state;
        }
    }
    return nullptr;
}

const Clip* FindClip(const AnimationSet& set, const AnimState& state) noexcept
{
    for (const Clip& clip : set.clips)
    {
        if (clip.name == state.clip)
        {
            return &clip;
        }
    }
    return nullptr;
}

float Wrap(float time, float duration, bool loop) noexcept
{
    if (!loop || duration <= 0.0f)
    {
        return std::min(time, duration);
    }
    return time - std::floor(time / duration) * duration;
}

void SampleState(const AnimationSet& set, const AnimState& state, float time, Pose& out)
{
    if (const Clip* clip = FindClip(set, state))
    {
        SampleClip(set.skeleton, *clip, time, state.loop, out);
    }
    else
    {
        out = BindPose(set.skeleton);
    }
}

float Advance(const AnimationSet& set, const AnimState& state, float time, float dt) noexcept
{
    const Clip* clip = FindClip(set, state);
    return Wrap(time + dt * state.speed, clip != nullptr ? clip->duration : 0.0f, state.loop);
}

void Begin(const AnimationSet& set, AnimatorComponent& a, std::uint32_t target, float duration)
{
    if (a.next != 0)
    {
        a.state = a.next;
        a.time = a.nextTime;
        a.next = 0;
    }
    const std::uint32_t current = a.state != 0 ? a.state : AnimNameHash(set.graph.defaultState);
    if (target == current || FindState(set.graph, target) == nullptr)
    {
        return;
    }
    if (duration <= 0.0f)
    {
        a.state = target;
        a.time = 0.0f;
        return;
    }
    a.next = target;
    a.nextTime = 0.0f;
    a.blendDuration = duration;
    a.blendElapsed = 0.0f;
}

} // namespace

std::uint32_t AnimNameHash(std::string_view name) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (const char c : name)
    {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

Result<AnimGraph> ParseAnimGraph(std::string_view text)
{
    const Json document = Json::parse(text.begin(), text.end(), nullptr, false);
    if (!document.is_object() || !document.contains("version") || !document["version"].is_number_integer())
    {
        return Invalid("animation graph: not a versioned JSON object");
    }
    if (document["version"].get<int>() != 1)
    {
        return MakeError(ErrorCode::VersionMismatch, "animation graph: unsupported version");
    }
    AnimGraph graph;
    if (document.contains("default"))
    {
        if (!document["default"].is_string())
        {
            return Invalid("animation graph: \"default\" must be a string");
        }
        graph.defaultState = document["default"].get<std::string>();
    }
    const auto text_field = [](const Json& entry, const char* key) -> Result<std::string>
    {
        if (!entry.contains(key) || !entry[key].is_string())
        {
            return Invalid(std::format("animation graph: \"{}\" must be a string", key));
        }
        return entry[key].get<std::string>();
    };
    if (document.contains("states"))
    {
        if (!document["states"].is_array())
        {
            return Invalid("animation graph: \"states\" must be an array");
        }
        for (const Json& entry : document["states"])
        {
            AnimState state;
            auto id = text_field(entry, "id");
            auto clip = text_field(entry, "clip");
            if (!id || !clip)
            {
                return Invalid(R"(animation graph: a state needs "id" and "clip")");
            }
            state.id = *id;
            state.clip = *clip;
            if (entry.contains("loop"))
            {
                if (!entry["loop"].is_boolean())
                {
                    return Invalid("animation graph: \"loop\" must be a bool");
                }
                state.loop = entry["loop"].get<bool>();
            }
            if (entry.contains("speed"))
            {
                if (!entry["speed"].is_number())
                {
                    return Invalid("animation graph: \"speed\" must be a number");
                }
                state.speed = entry["speed"].get<float>();
            }
            graph.states.push_back(std::move(state));
        }
    }
    if (document.contains("transitions"))
    {
        if (!document["transitions"].is_array())
        {
            return Invalid("animation graph: \"transitions\" must be an array");
        }
        for (const Json& entry : document["transitions"])
        {
            AnimTransition transition;
            auto from = text_field(entry, "from");
            auto to = text_field(entry, "to");
            auto trigger = text_field(entry, "trigger");
            if (!from || !to || !trigger)
            {
                return Invalid(R"(animation graph: a transition needs "from", "to" and "trigger")");
            }
            transition.from = *from;
            transition.to = *to;
            transition.trigger = *trigger;
            if (entry.contains("duration"))
            {
                if (!entry["duration"].is_number())
                {
                    return Invalid("animation graph: \"duration\" must be a number");
                }
                transition.duration = entry["duration"].get<float>();
            }
            graph.transitions.push_back(std::move(transition));
        }
    }
    return graph;
}

std::string WriteAnimGraph(const AnimGraph& graph)
{
    Json states = Json::array();
    for (const AnimState& state : graph.states)
    {
        states.push_back({{"id", state.id}, {"clip", state.clip}, {"loop", state.loop}, {"speed", state.speed}});
    }
    Json transitions = Json::array();
    for (const AnimTransition& t : graph.transitions)
    {
        transitions.push_back({{"from", t.from}, {"to", t.to}, {"trigger", t.trigger}, {"duration", t.duration}});
    }
    return Json{{"version", 1},
                {"default", graph.defaultState},
                {"states", std::move(states)},
                {"transitions", std::move(transitions)}}
               .dump(2) +
           "\n";
}

Result<std::uint32_t> AnimationLibrary::Add(AnimationSet set)
{
    if (set.id.empty() || Find(set.id) != nullptr)
    {
        return Invalid(std::format("animation set '{}': empty or repeated id", set.id));
    }
    INTERI_TRY(ValidateSkeleton(set.skeleton));
    std::set<std::string> clips;
    for (const Clip& clip : set.clips)
    {
        INTERI_TRY(ValidateClip(clip, set.skeleton));
        if (!clips.insert(clip.name).second)
        {
            return Invalid(std::format("animation set '{}': duplicate clip '{}'", set.id, clip.name));
        }
    }
    std::set<std::string> states;
    for (const AnimState& state : set.graph.states)
    {
        if (state.id.empty() || !states.insert(state.id).second || !clips.contains(state.clip) ||
            !std::isfinite(state.speed))
        {
            return Invalid(std::format(
                "animation set '{}': state '{}' is duplicate, has an unknown clip or a bad speed", set.id, state.id));
        }
    }
    if (!set.graph.states.empty() && !states.contains(set.graph.defaultState))
    {
        return Invalid(std::format("animation set '{}': unknown default state '{}'", set.id, set.graph.defaultState));
    }
    for (const AnimTransition& t : set.graph.transitions)
    {
        if ((t.from != "*" && !states.contains(t.from)) || !states.contains(t.to) || t.trigger.empty() ||
            !std::isfinite(t.duration) || t.duration < 0.0f)
        {
            return Invalid(std::format("animation set '{}': bad transition {} -> {}", set.id, t.from, t.to));
        }
    }
    m_sets.push_back(std::move(set));
    return static_cast<std::uint32_t>(m_sets.size() - 1);
}

const AnimationSet* AnimationLibrary::Find(std::string_view setId) const noexcept
{
    const auto it = std::ranges::find(m_sets, setId, &AnimationSet::id);
    return it == m_sets.end() ? nullptr : &*it;
}

Result<void> AnimationLibrary::BindArchetype(std::string_view archetype, std::string_view setId)
{
    const auto it = std::ranges::find(m_sets, setId, &AnimationSet::id);
    if (it == m_sets.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("no animation set '{}'", setId));
    }
    m_archetypes[std::string(archetype)] = static_cast<std::uint32_t>(it - m_sets.begin());
    return {};
}

Result<AnimatorComponent> AnimationLibrary::MakeAnimator(std::string_view archetype) const
{
    const auto it = m_archetypes.find(archetype);
    if (it == m_archetypes.end())
    {
        return MakeError(ErrorCode::NotFound, std::format("archetype '{}' has no animation set", archetype));
    }
    AnimatorComponent animator;
    animator.set = it->second;
    return animator;
}

namespace
{

Result<std::string> ReadText(const IFileSystem& files, const std::string& path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

Result<void> LoadModuleAnimations(const sdk::ModuleManifest& manifest, const IFileSystem& moduleFiles,
                                  AnimationLibrary& library)
{
    for (const sdk::AnimationSetDecl& decl : manifest.animationSets)
    {
        AnimationSet set;
        set.id = std::format("{}:{}", manifest.id, decl.name);
        INTERI_TRY_ASSIGN(skeletonText, ReadText(moduleFiles, decl.skeleton));
        INTERI_TRY_ASSIGN(skeleton, ParseSkeleton(skeletonText));
        set.skeleton = std::move(skeleton);
        for (const std::string& path : decl.clips)
        {
            INTERI_TRY_ASSIGN(clipText, ReadText(moduleFiles, path));
            auto clip = ParseClip(clipText, set.skeleton);
            if (!clip)
            {
                return MakeError(clip.error().code, std::format("{}: {}", path, clip.error().message));
            }
            set.clips.push_back(std::move(*clip));
        }
        INTERI_TRY_ASSIGN(graphText, ReadText(moduleFiles, decl.graph));
        auto graph = ParseAnimGraph(graphText);
        if (!graph)
        {
            return MakeError(graph.error().code, std::format("{}: {}", decl.graph, graph.error().message));
        }
        set.graph = std::move(*graph);
        const std::string id = set.id;
        INTERI_TRY(library.Add(std::move(set)));
        for (const std::string& archetype : decl.archetypes)
        {
            INTERI_TRY(library.BindArchetype(archetype, id));
        }
    }
    return {};
}

void StepAnimator(const AnimationLibrary& library, AnimatorComponent& a, float dt, Pose& out)
{
    const AnimationSet& set = library.Get(a.set);
    if (a.requestTrigger != 0)
    {
        const std::uint32_t current =
            a.next != 0 ? a.next : (a.state != 0 ? a.state : AnimNameHash(set.graph.defaultState));
        for (const AnimTransition& t : set.graph.transitions)
        {
            if (AnimNameHash(t.trigger) == a.requestTrigger && (t.from == "*" || AnimNameHash(t.from) == current))
            {
                Begin(set, a, AnimNameHash(t.to), t.duration);
                break;
            }
        }
        a.requestTrigger = 0;
    }
    if (a.requestState != 0)
    {
        Begin(set, a, a.requestState, a.requestBlend);
        a.requestState = 0;
        a.requestBlend = 0.0f;
    }

    const AnimState* current = FindState(set.graph, a.state);
    if (current == nullptr)
    {
        out = BindPose(set.skeleton);
        return;
    }
    a.time = Advance(set, *current, a.time, dt);
    SampleState(set, *current, a.time, out);
    if (a.next != 0)
    {
        if (const AnimState* next = FindState(set.graph, a.next))
        {
            a.nextTime = Advance(set, *next, a.nextTime, dt);
            a.blendElapsed += dt;
            if (a.blendElapsed >= a.blendDuration)
            {
                a.state = a.next;
                a.time = a.nextTime;
                a.next = 0;
                SampleState(set, *next, a.time, out);
            }
            else
            {
                Pose target;
                SampleState(set, *next, a.nextTime, target);
                BlendPoses(out, target, a.blendElapsed / a.blendDuration, out);
            }
        }
        else
        {
            a.next = 0;
        }
    }
}

} // namespace interi::anim
