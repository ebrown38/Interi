// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/chain/Executor.h>
#include <interi/core/Assert.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/ecs/Query.h>
#include <interi/jobs/ParallelAlgorithms.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <utility>

namespace interi::chain
{

using values::Value;

namespace
{

constexpr std::string_view kLogCategory = "Chain";

/// Whole fixed steps for a delay: rounded up, at least one.
[[nodiscard]] std::uint64_t DelaySteps(std::int64_t nanoseconds, std::int64_t stepNanoseconds)
{
    const std::int64_t steps = (std::max<std::int64_t>(nanoseconds, 0) + stepNanoseconds - 1) / stepNanoseconds;
    return static_cast<std::uint64_t>(std::max<std::int64_t>(steps, 1));
}

[[nodiscard]] Result<Value> Add(const Value& current, const Value& delta)
{
    if (values::TypeOf(current) != values::TypeOf(delta))
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("can't add {} to {}", values::ToString(values::TypeOf(delta)),
                                     values::ToString(values::TypeOf(current))));
    }
    return std::visit(
        [&](const auto& a) -> Result<Value>
        {
            using T = std::decay_t<decltype(a)>;
            const T& b = std::get<T>(delta);
            if constexpr (std::is_same_v<T, std::int32_t>)
            {
                return Value(static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b)));
            }
            else if constexpr (std::is_same_v<T, std::uint32_t>)
            {
                return Value(static_cast<std::uint32_t>(a + b));
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                return Value(a + b);
            }
            else if constexpr (std::is_same_v<T, Vec3>)
            {
                return Value(Vec3(a.x + b.x, a.y + b.y, a.z + b.z));
            }
            else
            {
                return MakeError(ErrorCode::InvalidArgument,
                                 std::format("can't add to a {}", values::ToString(values::TypeOf(current))));
            }
        },
        current.Variant());
}

} // namespace

// --- RuntimeValues -----------------------------------------------------------------------------------------------

const Value* RuntimeValues::Read(std::string_view id) const
{
    if (m_registry == nullptr)
    {
        return nullptr;
    }
    const values::ValueDefinition* definition = m_registry->Definition(id);
    if (definition == nullptr)
    {
        return nullptr;
    }
    if (const auto it = m_values.find(id);
        it != m_values.end() && values::TypeOf(it->second) == definition->schema.type)
    {
        return &it->second;
    }
    return m_registry->Get(id);
}

Result<bool> RuntimeValues::Set(std::string_view id, Value value)
{
    const values::ValueDefinition* definition = m_registry != nullptr ? m_registry->Definition(id) : nullptr;
    if (definition == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no Value '{}'", id));
    }
    if (auto valid = values::ValidateValue(definition->schema, value); !valid)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("Value '{}': {}", id, valid.error().message));
    }
    const Value* current = Read(id);
    const bool changed = current == nullptr || !(*current == value);
    if (const auto it = m_values.find(id); it != m_values.end())
    {
        it->second = std::move(value);
    }
    else
    {
        m_values.emplace(std::string(id), std::move(value));
    }
    return changed;
}

Result<bool> RuntimeValues::Modify(std::string_view id, const Value& delta)
{
    const Value* current = Read(id);
    if (current == nullptr)
    {
        return MakeError(ErrorCode::NotFound, std::format("no Value '{}'", id));
    }
    auto sum = Add(*current, delta);
    if (!sum)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("Value '{}': {}", id, sum.error().message));
    }
    return Set(id, std::move(*sum));
}

// --- ApplyContext ------------------------------------------------------------------------------------------------

std::optional<Entity> ApplyContext::FindEntity(std::uint64_t id) const
{
    const auto it = m_entities->find(id);
    if (it == m_entities->end() || !m_world->IsAlive(it->second))
    {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Transform> ApplyContext::GetTransform(Entity entity) const
{
    if (const auto it = m_transforms.find(entity); it != m_transforms.end())
    {
        return it->second;
    }
    if (const Transform* transform = m_world->IsAlive(entity) ? m_world->Get<Transform>(entity) : nullptr)
    {
        return *transform;
    }
    return std::nullopt;
}

void ApplyContext::SetTransform(Entity entity, const Transform& transform)
{
    m_transforms.insert_or_assign(entity, transform);
}

void ApplyContext::SetValue(std::string_view id, Value value)
{
    auto changed = m_values->Set(id, std::move(value));
    if (!changed)
    {
        m_executor->RejectWrite(changed.error().message);
        return;
    }
    if (*changed)
    {
        RaiseTrigger(kTriggerValueChanged, 0, std::string(id));
    }
}

void ApplyContext::ModifyValue(std::string_view id, const Value& delta)
{
    auto changed = m_values->Modify(id, delta);
    if (!changed)
    {
        m_executor->RejectWrite(changed.error().message);
        return;
    }
    if (*changed)
    {
        RaiseTrigger(kTriggerValueChanged, 0, std::string(id));
    }
}

void ApplyContext::RaiseTrigger(std::string_view trigger, std::uint64_t entity, std::string key,
                                std::vector<Value> payload)
{
    m_executor->RaiseTrigger(trigger, entity, std::move(key), std::move(payload));
}

void ApplyContext::PlaySound(SoundRequest request)
{
    m_executor->m_sounds.push_back(std::move(request));
}

// --- ChainExecutor -----------------------------------------------------------------------------------------------

struct ChainExecutor::Instance
{
    ChainAsset asset;
    std::uint64_t entity = 0;
    std::shared_ptr<const ChainProgram> program;
    std::vector<Diagnostic> diagnostics;
    bool started = false;
    std::uint64_t startStep = 0;
};

struct ChainExecutor::Firing
{
    std::int32_t priority = 0;
    std::uint64_t entity = 0;
    std::uint64_t sequence = 0;
    ChainId chain = 0;
    std::uint32_t node = 0;
    std::uint32_t output = 0;
    std::vector<Value> payload;
};

struct ChainExecutor::Continuation
{
    std::uint64_t due = 0;
    std::uint64_t sequence = 0;
    ChainId chain = 0;
    std::string node;
    std::uint32_t output = 0;
    std::uint64_t entity = 0;
    std::vector<Value> payload;
};

struct ChainExecutor::Raised
{
    std::string trigger;
    std::uint64_t entity = 0;
    std::string key;
    std::vector<Value> payload;
};

ChainExecutor::ChainExecutor(ChainRegistry& registry, values::ValueRegistry* values, ExecutorDesc desc)
    : m_registry(&registry)
    , m_valueRegistry(values)
    , m_desc(std::move(desc))
    , m_values(values)
{
    INTERI_ASSERT(m_desc.stepNanoseconds > 0, "the fixed step must be positive");
    m_desc.stepNanoseconds = std::max<std::int64_t>(m_desc.stepNanoseconds, 1);
    m_desc.grain = std::max<std::size_t>(m_desc.grain, 1);
    // Drop every program at once: it holds implementations a reloading module is about to unload.
    const auto invalidate = [this](std::string_view)
    {
        m_dirty = true;
        for (const auto& instance : m_instances)
        {
            if (instance)
            {
                instance->program.reset();
            }
        }
    };
    m_subscriptions = registry.Subscribe(invalidate);
    if (values != nullptr)
    {
        m_subscriptions.push_back(values->Subscribe(invalidate));
    }
}

ChainExecutor::~ChainExecutor() = default;

std::size_t ChainExecutor::PendingContinuations() const noexcept
{
    return m_continuations.size();
}

ChainExecutor::ChainId ChainExecutor::AddChain(ChainAsset asset, std::uint64_t entity)
{
    auto instance = std::make_unique<Instance>();
    instance->asset = std::move(asset);
    instance->entity = entity;
    CompileResult result = CompileChain(instance->asset, *m_registry, m_valueRegistry);
    instance->program = std::move(result.program);
    instance->diagnostics = std::move(result.diagnostics);
    const auto id = static_cast<ChainId>(m_instances.size());
    for (const Diagnostic& diagnostic : instance->diagnostics)
    {
        INTERI_LOG_WARN(kLogCategory, "chain {}: {}", id, diagnostic.Text());
    }
    m_instances.push_back(std::move(instance));
    return id;
}

void ChainExecutor::RemoveChain(ChainId chain)
{
    if (chain < m_instances.size())
    {
        m_instances[chain].reset();
    }
}

bool ChainExecutor::IsActive(ChainId chain) const
{
    return chain < m_instances.size() && m_instances[chain] != nullptr &&
           (m_instances[chain]->program != nullptr || m_dirty);
}

const std::vector<Diagnostic>& ChainExecutor::Diagnostics(ChainId chain) const
{
    static const std::vector<Diagnostic> kNone;
    return chain < m_instances.size() && m_instances[chain] ? m_instances[chain]->diagnostics : kNone;
}

void ChainExecutor::RaiseTrigger(std::string_view trigger, std::uint64_t entity, std::string key,
                                 std::vector<Value> payload)
{
    m_raised.push_back(
        {.trigger = std::string(trigger), .entity = entity, .key = std::move(key), .payload = std::move(payload)});
}

void ChainExecutor::RaiseSpawned(std::uint64_t entity)
{
    RaiseTrigger(kTriggerSpawned, entity, {}, {values::EntityRef{.id = entity}});
}

void ChainExecutor::RaiseDestroyed(std::uint64_t entity)
{
    RaiseTrigger(kTriggerDestroyed, entity, {}, {values::EntityRef{.id = entity}});
}

void ChainExecutor::Recompile()
{
    m_dirty = false;
    for (std::size_t id = 0; id < m_instances.size(); ++id)
    {
        Instance* instance = m_instances[id].get();
        if (instance == nullptr)
        {
            continue;
        }
        CompileResult result = CompileChain(instance->asset, *m_registry, m_valueRegistry);
        instance->program = std::move(result.program);
        instance->diagnostics = std::move(result.diagnostics);
        for (const Diagnostic& diagnostic : instance->diagnostics)
        {
            INTERI_LOG_WARN(kLogCategory, "chain {} (recompiled): {}", id, diagnostic.Text());
        }
    }
}

void ChainExecutor::AddTriggerFirings(std::vector<Firing>& firings, std::string_view trigger, std::uint64_t entity,
                                      std::string_view key, const std::vector<Value>& payload,
                                      std::optional<ChainId> only)
{
    for (std::size_t id = 0; id < m_instances.size(); ++id)
    {
        const Instance* instance = m_instances[id].get();
        if (instance == nullptr || instance->program == nullptr || !instance->started || (only && *only != id))
        {
            continue;
        }
        std::uint64_t activationEntity = entity;
        if (instance->entity != 0)
        {
            if (entity != 0 && entity != instance->entity)
            {
                continue;
            }
            activationEntity = instance->entity;
        }
        const ChainProgram& program = *instance->program;
        for (std::size_t n = 0; n < program.nodes.size(); ++n)
        {
            const ProgramNode& node = program.nodes[n];
            if (node.kind != EventKind::Trigger || node.event != trigger || node.outputLinks.empty())
            {
                continue;
            }
            if (node.keyInput < node.triggerInputs.size())
            {
                const auto* nodeKey = std::get_if<std::string>(&node.triggerInputs[node.keyInput].Variant());
                if (nodeKey == nullptr || *nodeKey != key)
                {
                    continue;
                }
            }
            firings.push_back({.priority = program.priority,
                               .entity = activationEntity,
                               .sequence = m_sequence++,
                               .chain = static_cast<ChainId>(id),
                               .node = static_cast<std::uint32_t>(n),
                               .output = 0,
                               .payload = payload});
        }
    }
}

void ChainExecutor::CollectWave0(std::vector<Firing>& firings)
{
    // Start: chains that run for the first time.
    std::vector<std::size_t> starting;
    for (std::size_t id = 0; id < m_instances.size(); ++id)
    {
        Instance* instance = m_instances[id].get();
        if (instance != nullptr && instance->program != nullptr && !instance->started)
        {
            instance->started = true;
            instance->startStep = m_step;
            starting.push_back(id);
        }
    }
    for (const std::size_t id : starting)
    {
        AddTriggerFirings(firings, kTriggerStart, 0, {}, {}, static_cast<ChainId>(id));
    }

    // Raised triggers, in raise order.
    std::vector<Raised> raised = std::exchange(m_raised, {});
    for (const Raised& trigger : raised)
    {
        AddTriggerFirings(firings, trigger.trigger, trigger.entity, trigger.key, trigger.payload);
    }

    // Timers and ticks.
    const auto dt = static_cast<float>(static_cast<double>(m_desc.stepNanoseconds) * 1e-9);
    for (std::size_t id = 0; id < m_instances.size(); ++id)
    {
        const Instance* instance = m_instances[id].get();
        if (instance == nullptr || instance->program == nullptr || !instance->started)
        {
            continue;
        }
        const ChainProgram& program = *instance->program;
        const std::uint64_t elapsed = m_step - instance->startStep;
        for (std::size_t n = 0; n < program.nodes.size(); ++n)
        {
            const ProgramNode& node = program.nodes[n];
            if (node.kind != EventKind::Trigger || node.outputLinks.empty())
            {
                continue;
            }
            std::vector<Value> payload;
            if (node.event == kTriggerTick)
            {
                payload = {static_cast<std::int32_t>(static_cast<std::uint32_t>(m_step)), dt};
            }
            else if (node.event == kTriggerTimer && !node.triggerInputs.empty())
            {
                const auto* interval = std::get_if<float>(&node.triggerInputs[0].Variant());
                const std::uint64_t every =
                    DelaySteps(SecondsToNanoseconds(interval != nullptr ? *interval : 1.0f), m_desc.stepNanoseconds);
                if (elapsed == 0 || elapsed % every != 0)
                {
                    continue;
                }
                payload = {static_cast<std::int32_t>(static_cast<std::uint32_t>(elapsed / every))};
            }
            else
            {
                continue;
            }
            firings.push_back({.priority = program.priority,
                               .entity = instance->entity,
                               .sequence = m_sequence++,
                               .chain = static_cast<ChainId>(id),
                               .node = static_cast<std::uint32_t>(n),
                               .output = 0,
                               .payload = std::move(payload)});
        }
    }

    // Continuations that are due, in the order they were scheduled.
    std::vector<Continuation> due;
    std::erase_if(m_continuations,
                  [&](Continuation& continuation)
                  {
                      if (continuation.due > m_step)
                      {
                          return false;
                      }
                      due.push_back(std::move(continuation));
                      return true;
                  });
    std::ranges::sort(due, {}, &Continuation::sequence);
    for (Continuation& continuation : due)
    {
        const Instance* instance =
            continuation.chain < m_instances.size() ? m_instances[continuation.chain].get() : nullptr;
        if (instance == nullptr || instance->program == nullptr)
        {
            continue;
        }
        const std::size_t node = instance->program->NodeIndex(continuation.node);
        if (node == instance->program->nodes.size() ||
            continuation.output >= instance->program->nodes[node].outputLinks.size())
        {
            continue; // the chain changed under it
        }
        firings.push_back({.priority = instance->program->priority,
                           .entity = continuation.entity,
                           .sequence = m_sequence++,
                           .chain = continuation.chain,
                           .node = static_cast<std::uint32_t>(node),
                           .output = continuation.output,
                           .payload = std::move(continuation.payload)});
    }
}

void ChainExecutor::SortFirings(std::vector<Firing>& firings) const
{
    // Sequences are unique, so the order is total.
    std::ranges::sort(firings,
                      [](const Firing& a, const Firing& b)
                      {
                          if (a.priority != b.priority)
                          {
                              return a.priority < b.priority;
                          }
                          if (a.entity != b.entity)
                          {
                              return a.entity < b.entity;
                          }
                          return a.sequence < b.sequence;
                      });
}

bool ChainExecutor::HasAuthority(Authority authority, std::uint64_t entity) const
{
    if (m_desc.role == ExecutorRole::Server || authority == Authority::Any)
    {
        return true;
    }
    return authority == Authority::Owner && m_desc.isOwner && m_desc.isOwner(entity);
}

void ChainExecutor::AddTrace(TraceRecord record)
{
    if (m_desc.trace)
    {
        m_trace.push_back(std::move(record));
    }
}

void ChainExecutor::RejectWrite(const std::string& message)
{
    INTERI_LOG_WARN(kLogCategory, "write dropped: {}", message);
    AddTrace({.kind = TraceRecord::Kind::WriteRejected,
              .step = m_step,
              .chain = 0,
              .node = {},
              .output = {},
              .link = 0,
              .entity = 0,
              .passed = false,
              .payload = {},
              .message = message});
}

void ChainExecutor::Step(World& world, JobSystem& jobs, CommandBuffers& commands)
{
    m_trace.clear();
    m_sounds.clear();
    m_stats = {};
    if (m_dirty)
    {
        Recompile();
    }

    std::map<std::uint64_t, Entity> entities;
    Query().With<StableIdComponent>().ForEachChunk(world,
                                                   [&](ChunkView& view)
                                                   {
                                                       const auto ids = view.Read<StableIdComponent>();
                                                       const auto handles = view.Entities();
                                                       for (std::size_t i = 0; i < ids.size(); ++i)
                                                       {
                                                           entities.emplace(ids[i].id, handles[i]);
                                                       }
                                                   });

    std::vector<Firing> firings;
    CollectWave0(firings);
    SortFirings(firings);

    struct Activation
    {
        std::uint32_t firing;
        std::uint32_t link;
    };
    struct Outcome
    {
        bool passed = false;
        bool authoritySkip = false;
        std::vector<Value> inputs;
        std::vector<EventCall::Fired> fired;
        std::vector<Effect> effects;
    };

    std::vector<Effect> effects;
    std::uint32_t remaining = m_desc.maxActivationsPerStep;
    const World& readOnlyWorld = world;
    while (!firings.empty())
    {
        std::vector<Activation> activations;
        for (std::size_t f = 0; f < firings.size(); ++f)
        {
            const Firing& firing = firings[f];
            const ProgramNode& node = m_instances[firing.chain]->program->nodes[firing.node];
            AddTrace({.kind = TraceRecord::Kind::Fired,
                      .step = m_step,
                      .chain = firing.chain,
                      .node = node.id,
                      .output = node.outputs[firing.output],
                      .link = 0,
                      .entity = firing.entity,
                      .passed = false,
                      .payload = m_desc.trace ? firing.payload : std::vector<Value>{},
                      .message = {}});
            const auto [first, end] = node.outputLinks[firing.output];
            for (std::uint32_t link = first; link < end; ++link)
            {
                activations.push_back({.firing = static_cast<std::uint32_t>(f), .link = link});
            }
        }
        if (activations.empty())
        {
            break;
        }
        if (m_stats.waves == m_desc.maxWavesPerStep)
        {
            m_stats.depthExceeded = true;
            m_stats.dropped += static_cast<std::uint32_t>(activations.size());
            INTERI_LOG_WARN(kLogCategory, "step {}: chains went {} waves deep; dropped {} activations", m_step,
                            m_desc.maxWavesPerStep, activations.size());
            AddTrace({.kind = TraceRecord::Kind::Dropped,
                      .step = m_step,
                      .chain = 0,
                      .node = {},
                      .output = {},
                      .link = 0,
                      .entity = 0,
                      .passed = false,
                      .payload = {},
                      .message = "depth limit"});
            break;
        }
        ++m_stats.waves;
        if (activations.size() > remaining)
        {
            const std::size_t dropped = activations.size() - remaining;
            activations.resize(remaining);
            m_stats.dropped += static_cast<std::uint32_t>(dropped);
            if (!m_stats.budgetExceeded)
            {
                INTERI_LOG_WARN(kLogCategory, "step {}: the event budget ({}) is exhausted; dropping the rest", m_step,
                                m_desc.maxActivationsPerStep);
            }
            m_stats.budgetExceeded = true;
            AddTrace({.kind = TraceRecord::Kind::Dropped,
                      .step = m_step,
                      .chain = 0,
                      .node = {},
                      .output = {},
                      .link = 0,
                      .entity = 0,
                      .passed = false,
                      .payload = {},
                      .message = "event budget"});
        }
        remaining -= static_cast<std::uint32_t>(activations.size());
        m_stats.activations += static_cast<std::uint32_t>(activations.size());

        // Evaluate: pure given the pre-step state, so chunks may run on any worker.
        std::vector<Outcome> outcomes(activations.size());
        ParallelFor(
            jobs, 0, activations.size(), m_desc.grain,
            [&](std::size_t i)
            {
                const Activation& activation = activations[i];
                const Firing& firing = firings[activation.firing];
                const ChainProgram& program = *m_instances[firing.chain]->program;
                const ProgramLink& link = program.links[activation.link];
                const ExprContext context{.payload = firing.payload, .values = &m_values, .self = firing.entity};
                Outcome& outcome = outcomes[i];
                if (link.condition.Valid())
                {
                    const Value result = EvaluateExpression(link.condition, context);
                    const bool* passed = std::get_if<bool>(&result.Variant());
                    if (passed == nullptr || !*passed)
                    {
                        return;
                    }
                }
                outcome.passed = true;
                const ProgramNode& target = program.nodes[link.target];
                if (!HasAuthority(target.authority, firing.entity))
                {
                    outcome.authoritySkip = true;
                    return;
                }
                outcome.inputs.reserve(link.inputs.size());
                for (const CompiledExpression& input : link.inputs)
                {
                    outcome.inputs.push_back(EvaluateExpression(input, context));
                }
                const std::uint64_t seed =
                    HashCombine(HashCombine(HashCombine(m_desc.seed, m_step), firing.sequence), activation.link);
                EventCall call(outcome.inputs, firing.entity, m_step, m_desc.stepNanoseconds, seed, readOnlyWorld,
                               m_values, target.outputLinks.size());
                (*target.implementation)(call);
                outcome.fired = std::move(call.m_fired);
                outcome.effects = std::move(call.m_effects);
            });

        // Merge in activation order.
        std::vector<Firing> next;
        for (std::size_t i = 0; i < activations.size(); ++i)
        {
            const Activation& activation = activations[i];
            const Firing& firing = firings[activation.firing];
            const ChainProgram& program = *m_instances[firing.chain]->program;
            const ProgramLink& link = program.links[activation.link];
            const ProgramNode& target = program.nodes[link.target];
            Outcome& outcome = outcomes[i];
            if (link.condition.Valid())
            {
                AddTrace({.kind = TraceRecord::Kind::Condition,
                          .step = m_step,
                          .chain = firing.chain,
                          .node = {},
                          .output = {},
                          .link = link.assetLink,
                          .entity = firing.entity,
                          .passed = outcome.passed,
                          .payload = {},
                          .message = {}});
            }
            if (!outcome.passed)
            {
                continue;
            }
            if (outcome.authoritySkip)
            {
                AddTrace({.kind = TraceRecord::Kind::AuthoritySkip,
                          .step = m_step,
                          .chain = firing.chain,
                          .node = target.id,
                          .output = {},
                          .link = link.assetLink,
                          .entity = firing.entity,
                          .passed = false,
                          .payload = {},
                          .message = {}});
                continue;
            }
            ++m_stats.eventsRun;
            AddTrace({.kind = TraceRecord::Kind::Ran,
                      .step = m_step,
                      .chain = firing.chain,
                      .node = target.id,
                      .output = {},
                      .link = link.assetLink,
                      .entity = firing.entity,
                      .passed = false,
                      .payload = m_desc.trace ? outcome.inputs : std::vector<Value>{},
                      .message = {}});
            for (EventCall::Fired& fired : outcome.fired)
            {
                if (fired.delayNanoseconds < 0)
                {
                    next.push_back({.priority = program.priority,
                                    .entity = firing.entity,
                                    .sequence = m_sequence++,
                                    .chain = firing.chain,
                                    .node = link.target,
                                    .output = static_cast<std::uint32_t>(fired.output),
                                    .payload = std::move(fired.payload)});
                }
                else
                {
                    m_continuations.push_back(
                        {.due = m_step + DelaySteps(fired.delayNanoseconds, m_desc.stepNanoseconds),
                         .sequence = m_sequence++,
                         .chain = firing.chain,
                         .node = target.id,
                         .output = static_cast<std::uint32_t>(fired.output),
                         .entity = firing.entity,
                         .payload = std::move(fired.payload)});
                }
            }
            for (Effect& effect : outcome.effects)
            {
                effects.push_back(std::move(effect));
            }
        }
        SortFirings(next);
        firings = std::move(next);
    }

    // Apply, serially, in activation order.
    m_stats.effects = static_cast<std::uint32_t>(effects.size());
    CommandBuffer& buffer = commands.Local(jobs);
    ApplyContext context(*this, world, m_values, buffer, entities);
    for (Effect& effect : effects)
    {
        effect(context);
    }
    for (const auto& [entity, transform] : context.m_transforms)
    {
        if (world.IsAlive(entity))
        {
            buffer.Add(context.NextCommandKey(), entity, transform);
        }
    }
    commands.Playback(world);
    ++m_step;
}

} // namespace interi::chain
