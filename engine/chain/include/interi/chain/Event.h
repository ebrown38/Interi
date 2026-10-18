// SPDX-License-Identifier: MIT
#pragma once

// Chain Reaction events: the nodes of a chain. An event has typed inputs, named outputs that each carry a payload,
// and either a native C++ implementation (an action) or none (a trigger, fired by the runtime: Start, Tick, Input
// Action, Timer, Value Changed, Spawned, Destroyed, and whatever triggers modules add).
//
// Events are contributions (sdk::ContributionRegistry): a module provides its local events ("com.author.fps:FireGun")
// or a declared shared id, and a higher module or Plugin overrides one with the same inputs and outputs.
//
// An implementation runs during a wave, possibly on a job worker, against the pre-step state: it reads its inputs,
// the World and Values (read-only), and records what it does: outputs to fire now or later, and effects (`Defer`)
// that the executor applies serially, in queue order, at the end of the step. It must not write shared state itself,
// so the result is the same at any thread count.

#include <interi/chain/Expression.h>
#include <interi/core/Result.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/Value.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace interi
{
class World;
} // namespace interi

namespace interi::chain
{

/// Who may run an event once games are networked (designed for, not built: the executor's role decides).
enum class Authority : std::uint8_t
{
    Any,    ///< Runs everywhere.
    Server, ///< Runs only with server authority.
    Owner,  ///< Runs on the server and on the client that owns the activation's entity.
};

/// One typed input of an event.
struct EventParam
{
    std::string name;
    /// Type, default (used when a node doesn't bind the input), range, enum options.
    values::ValueSchema schema;
    /// Non-empty: the input takes the type of the Value named by this other input's constant (Set Value's `value`
    /// has the type of the Value its `id` names). `schema` is ignored then.
    std::string typeFrom;
};

/// One named output and the payload it carries.
struct EventOutput
{
    std::string name;
    std::vector<PayloadField> payload;
};

enum class EventKind : std::uint8_t
{
    Action,  ///< Runs a native implementation when a link reaches it.
    Trigger, ///< Fired by the runtime; its inputs are constants that select what it reacts to.
};

class EventCall;

/// A native implementation.
using EventImpl = std::function<void(EventCall& call)>;

/// One provider's definition of an event.
struct EventDefinition
{
    EventKind kind = EventKind::Action;
    std::vector<EventParam> inputs;
    std::vector<EventOutput> outputs;
    /// Fires (some) outputs later ("On Finished"), through EventCall::FireAfter.
    bool latent = false;
    Authority authority = Authority::Any;
    std::string category;
    std::string tooltip;
    /// Actions only.
    std::shared_ptr<const EventImpl> implementation;

    [[nodiscard]] const EventOutput* FindOutput(std::string_view name) const noexcept;
    /// The index of input `name`, or inputs.size().
    [[nodiscard]] std::size_t InputIndex(std::string_view name) const noexcept;
};

/// Seconds to nanoseconds, rounded up and saturating; NaN and negatives are 0 (exact double arithmetic, so the same
/// on every platform).
[[nodiscard]] std::int64_t SecondsToNanoseconds(float seconds) noexcept;

class ApplyContext;

/// Something an event does to shared state, applied serially at the end of the step.
using Effect = std::move_only_function<void(ApplyContext& context)>;

/// What a native implementation sees and records (built by the executor for one activation).
class EventCall
{
public:
    EventCall(const EventCall&) = delete;
    EventCall& operator=(const EventCall&) = delete;
    EventCall(EventCall&&) = delete;
    EventCall& operator=(EventCall&&) = delete;
    ~EventCall() = default;

    /// Input `index` (in the definition's order), of the input's type.
    [[nodiscard]] const values::Value& Input(std::size_t index) const noexcept { return m_inputs[index]; }
    template <typename T>
    [[nodiscard]] const T& Get(std::size_t index) const
    {
        return std::get<T>(m_inputs[index]);
    }

    /// Fires output `index` now: links from it activate in the next wave of this step.
    void Fire(std::size_t output, std::vector<values::Value> payload = {});
    /// Fires output `index` after `nanoseconds` (rounded up to whole fixed steps, at least one).
    void FireAfter(std::int64_t nanoseconds, std::size_t output, std::vector<values::Value> payload = {});
    /// Records an effect for the serial apply phase at the end of the step.
    void Defer(Effect effect);

    /// The activation's entity (a stable id; 0 for none).
    [[nodiscard]] std::uint64_t Entity() const noexcept { return m_entity; }
    /// The fixed step being run (0 for the first).
    [[nodiscard]] std::uint64_t Step() const noexcept { return m_step; }
    [[nodiscard]] std::int64_t StepNanoseconds() const noexcept { return m_stepNanoseconds; }
    /// A seed unique to this activation and stable across runs and thread counts (for core/Random.h).
    [[nodiscard]] std::uint64_t Seed() const noexcept { return m_seed; }
    /// The pre-step world (read-only), and the pre-step Values.
    [[nodiscard]] const World& GetWorld() const noexcept { return *m_world; }
    [[nodiscard]] const IValueReader& Values() const noexcept { return *m_values; }

private:
    friend class ChainExecutor;

    struct Fired
    {
        std::size_t output;
        std::int64_t delayNanoseconds; ///< < 0: now.
        std::vector<values::Value> payload;
    };

    EventCall(std::span<const values::Value> inputs, std::uint64_t entity, std::uint64_t step,
              std::int64_t stepNanoseconds, std::uint64_t seed, const World& world, const IValueReader& values,
              std::size_t outputCount) noexcept
        : m_inputs(inputs)
        , m_entity(entity)
        , m_step(step)
        , m_stepNanoseconds(stepNanoseconds)
        , m_seed(seed)
        , m_world(&world)
        , m_values(&values)
        , m_outputCount(outputCount)
    {
    }

    std::span<const values::Value> m_inputs;
    std::uint64_t m_entity;
    std::uint64_t m_step;
    std::int64_t m_stepNanoseconds;
    std::uint64_t m_seed;
    const World* m_world;
    const IValueReader* m_values;
    std::size_t m_outputCount;
    std::vector<Fired> m_fired;
    std::vector<Effect> m_effects;
};

/// The events of a project. Not thread-safe to modify; concurrent reads are fine while nothing modifies it.
class EventRegistry
{
public:
    using Entries = std::vector<std::pair<std::string, sdk::ContributionRegistry<EventDefinition>::Entry>>;

    /// `modules` must outlive the registry.
    explicit EventRegistry(const sdk::ModuleSet& modules)
        : m_registry(modules)
    {
    }

    /// `provider` provides event `id`. InvalidArgument for an invalid definition (an action without an
    /// implementation, a trigger with one, duplicate or invalid input/output names, an invalid input schema, a
    /// `typeFrom` that names no string input) or the identity rule.
    Result<void> Provide(std::string_view provider, std::string_view id, EventDefinition definition);
    /// `provider` replaces a lower module's event `targetId`; kind, inputs (names and types) and outputs (names and
    /// payloads) must match (InvalidArgument).
    Result<void> Override(std::string_view provider, std::string_view targetId, EventDefinition definition);

    /// The winning definition, or nullptr.
    [[nodiscard]] const EventDefinition* Resolve(std::string_view id) const noexcept { return m_registry.Resolve(id); }

    [[nodiscard]] sdk::ContributionRegistry<EventDefinition>& Registry() noexcept { return m_registry; }
    [[nodiscard]] const sdk::ContributionRegistry<EventDefinition>& Registry() const noexcept { return m_registry; }

private:
    sdk::ContributionRegistry<EventDefinition> m_registry;
};

/// Everything chains compile against besides Values: events and functions.
class ChainRegistry
{
public:
    /// What one provider contributed, taken out for a rollback.
    struct Extracted
    {
        EventRegistry::Entries events;
        FunctionRegistry::Entries functions;
    };

    /// `modules` must outlive the registry.
    explicit ChainRegistry(const sdk::ModuleSet& modules)
        : m_events(modules)
        , m_functions(modules)
    {
    }

    [[nodiscard]] EventRegistry& Events() noexcept { return m_events; }
    [[nodiscard]] const EventRegistry& Events() const noexcept { return m_events; }
    [[nodiscard]] FunctionRegistry& Functions() noexcept { return m_functions; }
    [[nodiscard]] const FunctionRegistry& Functions() const noexcept { return m_functions; }

    [[nodiscard]] Extracted ExtractProvider(std::string_view provider);
    void RemoveProvider(std::string_view provider) { (void)ExtractProvider(provider); }
    void Restore(Extracted extracted);

    /// Calls `callback(id)` when an event or function's winner may have changed.
    [[nodiscard]] std::vector<sdk::Subscription> Subscribe(const std::function<void(std::string_view id)>& callback);

private:
    EventRegistry m_events;
    FunctionRegistry m_functions;
};

} // namespace interi::chain
