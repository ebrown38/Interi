// SPDX-License-Identifier: MIT
#pragma once

// Runs chains on the fixed step, deterministically.
//
// Each Step:
//   1. Wave 0: the triggers raised since the last step (Start for new chains, Input Action, Spawned, Value Changed
//      from the last step's writes...), Timer and Tick triggers, and latent continuations that are due. Each is a
//      fired output; they are ordered by (chain priority, entity id, sequence).
//   2. Each wave expands its fired outputs into link activations (link order), cut to what is left of the step's
//      activation budget, and evaluates them in parallel: the link's condition, then the target's inputs, then its
//      native implementation. Everything reads the pre-step state: nothing shared is written during the waves.
//      Outputs fired now form the next wave (ordered the same way); FireAfter schedules a continuation on a later
//      step. Waves stop at the depth limit.
//   3. Apply: every recorded effect runs serially, in activation order. Deltas (Modify Value, Move) resolve against
//      the current value at that point, so two hits in one step both land; absolute writes (Set Value) are last
//      writer wins. Structural ECS changes are recorded in CommandBuffers and played back at the end.
// The result is bit-identical at any worker count. A step that exceeds the budget or the depth logs a warning and
// drops the rest: a runaway chain never hangs the game.
//
// Programs are dropped as soon as the event, function or Value registry changes (a reloading module's code must not
// outlive it) and recompiled at the next Step; a chain that no longer compiles is disabled and its diagnostics kept.

#include <interi/chain/ChainAsset.h>
#include <interi/chain/Compiler.h>
#include <interi/chain/Event.h>
#include <interi/chain/Expression.h>
#include <interi/core/math/Transform.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ValueRegistry.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::chain
{

/// Gives an entity the stable id chains name it by (entity refs, `self`). Ids are unique within a world.
struct StableIdComponent
{
    static constexpr const char* kComponentName = "interi.StableId";
    std::uint64_t id = 0;
};

/// Tag: the entity is hidden (world.Hide); renderers skip it.
struct HiddenComponent
{
    static constexpr const char* kComponentName = "interi.Hidden";
};

/// A sound a chain asked for (world.PlaySound); the host plays it.
struct SoundRequest
{
    std::string cue;
    std::uint64_t entity = 0; ///< 0: not attached.
    float volume = 1.0f;
};

/// Chain-set Values: a run-time layer above the project's Values (run time > project > module default). Reads are
/// thread-safe while nothing writes.
class RuntimeValues final : public IValueReader
{
public:
    /// `registry` may be nullptr (no Values); it must outlive this.
    explicit RuntimeValues(const values::ValueRegistry* registry) noexcept
        : m_registry(registry)
    {
    }

    /// The run-time value if set (and still of the definition's type), else the registry's effective value.
    [[nodiscard]] const values::Value* Read(std::string_view id) const override;
    /// Sets the run-time value. True if the effective value changed. NotFound for an undefined id, InvalidArgument if
    /// the value doesn't fit the definition (wrong type, out of range, not finite).
    Result<bool> Set(std::string_view id, values::Value value);
    /// Adds `delta` to the current value (int and u32 wrap; float; vec3). Same errors as Set, plus InvalidArgument for
    /// a non-numeric Value or a delta of another type.
    Result<bool> Modify(std::string_view id, const values::Value& delta);
    /// Drops every run-time value.
    void Clear() noexcept { m_values.clear(); }
    [[nodiscard]] const std::map<std::string, values::Value, std::less<>>& Overrides() const noexcept
    {
        return m_values;
    }

private:
    const values::ValueRegistry* m_registry;
    std::map<std::string, values::Value, std::less<>> m_values;
};

/// A prefab a chain asked to spawn (world.SpawnPrefab). The host expands it after the step (scene::SpawnPrefabs)
/// and raises trigger.Spawned for its root.
struct SpawnRequest
{
    std::string prefab;   ///< Virtual path of a .prefab.json.
    std::uint64_t id = 0; ///< The stable id the spawned root takes (allocated in effect order).
    Transform transform;  ///< The root's world transform.
};

/// One record of the debug trace (for the M21 debugger).
struct TraceRecord
{
    enum class Kind : std::uint8_t
    {
        Fired,         ///< A node's output fired (payload = the output's payload).
        Condition,     ///< A link's condition was evaluated (passed = its result).
        Ran,           ///< An action ran (payload = its inputs).
        AuthoritySkip, ///< An action was skipped: this executor lacks its authority.
        Dropped,       ///< Activations dropped by the budget or depth limit.
        WriteRejected, ///< An effect's Value write didn't fit the Value's definition.
    };

    Kind kind = Kind::Fired;
    std::uint64_t step = 0;
    std::uint32_t chain = 0;  ///< ChainExecutor::ChainId.
    std::string node;         ///< Node id (Fired, Ran, AuthoritySkip); empty otherwise.
    std::string output;       ///< Output name (Fired).
    std::size_t link = 0;     ///< Asset link index (Condition).
    std::uint64_t entity = 0; ///< The activation's entity.
    bool passed = false;      ///< Condition.
    std::vector<values::Value> payload;
    std::string message; ///< Dropped, WriteRejected.
};

enum class ExecutorRole : std::uint8_t
{
    Server, ///< Runs every event (also single player).
    Client, ///< Skips Server events, and Owner events for entities it doesn't own.
};

struct ExecutorDesc
{
    std::int64_t stepNanoseconds = 16'666'667; ///< The fixed step (FixedStepClock's).
    std::uint32_t maxActivationsPerStep = 4096;
    std::uint32_t maxWavesPerStep = 64;
    std::uint64_t seed = 0;
    ExecutorRole role = ExecutorRole::Server;
    std::function<bool(std::uint64_t entity)> isOwner; ///< Client: does this client own the entity?
    bool trace = false;
    std::size_t grain = 16; ///< Activations per parallel chunk.
    /// Stable ids of spawned prefab roots count up from here (above any authored scene id).
    std::uint64_t firstSpawnId = std::uint64_t{1} << 62;
};

struct ExecutorStats
{
    std::uint32_t activations = 0; ///< Link activations evaluated.
    std::uint32_t eventsRun = 0;   ///< Actions run.
    std::uint32_t waves = 0;
    std::uint32_t effects = 0;
    std::uint32_t dropped = 0; ///< Activations dropped by the budget or depth limit.
    bool budgetExceeded = false;
    bool depthExceeded = false;
};

class ChainExecutor;

/// What an effect sees in the serial apply phase.
class ApplyContext
{
public:
    ApplyContext(const ApplyContext&) = delete;
    ApplyContext& operator=(const ApplyContext&) = delete;
    ApplyContext(ApplyContext&&) = delete;
    ApplyContext& operator=(ApplyContext&&) = delete;
    ~ApplyContext() = default;

    /// The world as of the start of the step plus nothing else: structural changes and transforms are applied when
    /// the step ends. Read it; write through Commands() or SetTransform().
    [[nodiscard]] const World& GetWorld() const noexcept { return *m_world; }
    [[nodiscard]] RuntimeValues& Values() noexcept { return *m_values; }
    /// Records a structural change; use NextCommandKey() for its key so playback follows effect order.
    [[nodiscard]] CommandBuffer& Commands() noexcept { return *m_commands; }
    [[nodiscard]] std::uint64_t NextCommandKey() noexcept { return m_nextKey++; }

    /// The live entity with stable id `id`.
    [[nodiscard]] std::optional<Entity> FindEntity(std::uint64_t id) const;
    /// The entity's transform including earlier effects of this step; nullopt if it has none.
    [[nodiscard]] std::optional<Transform> GetTransform(Entity entity) const;
    /// Sets the entity's transform (written when the step ends).
    void SetTransform(Entity entity, const Transform& transform);

    /// Writes a Value (Set or Modify); a write that doesn't fit is dropped, logged and traced. Changes raise
    /// trigger.ValueChanged next step.
    void SetValue(std::string_view id, values::Value value);
    void ModifyValue(std::string_view id, const values::Value& delta);

    /// Raises a trigger for the next step.
    void RaiseTrigger(std::string_view trigger, std::uint64_t entity, std::string key,
                      std::vector<values::Value> payload = {});
    void PlaySound(SoundRequest request);
    /// Asks the host to spawn a prefab after the step; returns the stable id its root will have.
    std::uint64_t SpawnPrefab(std::string prefab, const Transform& transform);

private:
    friend class ChainExecutor;

    ApplyContext(ChainExecutor& executor, const World& world, RuntimeValues& values, CommandBuffer& commands,
                 const std::map<std::uint64_t, Entity>& entities) noexcept
        : m_executor(&executor)
        , m_world(&world)
        , m_values(&values)
        , m_commands(&commands)
        , m_entities(&entities)
    {
    }

    ChainExecutor* m_executor;
    const World* m_world;
    RuntimeValues* m_values;
    CommandBuffer* m_commands;
    const std::map<std::uint64_t, Entity>* m_entities;
    std::map<Entity, Transform> m_transforms; ///< Ordered by handle: flushed deterministically.
    std::uint64_t m_nextKey = 0;
};

class ChainExecutor
{
public:
    using ChainId = std::uint32_t;

    /// `registry` and `values` (may be nullptr) must outlive the executor; it subscribes to their changes.
    ChainExecutor(ChainRegistry& registry, values::ValueRegistry* values, ExecutorDesc desc = {});

    ChainExecutor(const ChainExecutor&) = delete;
    ChainExecutor& operator=(const ChainExecutor&) = delete;
    ChainExecutor(ChainExecutor&&) = delete;
    ChainExecutor& operator=(ChainExecutor&&) = delete;
    ~ChainExecutor();

    /// Adds a chain, bound to `entity` (0: a global chain that reacts to every entity's triggers), and compiles it.
    /// A chain that doesn't compile is kept but inactive (see Diagnostics). Its Start trigger fires on the next Step.
    ChainId AddChain(ChainAsset asset, std::uint64_t entity = 0);
    void RemoveChain(ChainId chain);
    [[nodiscard]] bool IsActive(ChainId chain) const;
    [[nodiscard]] const std::vector<Diagnostic>& Diagnostics(ChainId chain) const;

    /// Raises a trigger for the next Step. A chain bound to an entity reacts to triggers raised for it or for no
    /// entity (0); a global chain reacts to all, and its activation's entity is the raised one. `key` is matched
    /// against the trigger node's "key" input (when it has one).
    void RaiseTrigger(std::string_view trigger, std::uint64_t entity = 0, std::string key = {},
                      std::vector<values::Value> payload = {});
    void RaiseInputAction(std::string_view action) { RaiseTrigger(kInputActionTrigger, 0, std::string(action)); }
    void RaiseSpawned(std::uint64_t entity);
    void RaiseDestroyed(std::uint64_t entity);

    /// Runs one fixed step (see the top of this header). A Scheduler sync point: call it between Scheduler runs, on
    /// the thread that owns `world`; `commands` is played back before it returns.
    void Step(World& world, JobSystem& jobs, CommandBuffers& commands);

    [[nodiscard]] RuntimeValues& Values() noexcept { return m_values; }
    [[nodiscard]] const RuntimeValues& Values() const noexcept { return m_values; }
    /// The next step's index (the number of steps run).
    [[nodiscard]] std::uint64_t StepIndex() const noexcept { return m_step; }
    [[nodiscard]] const ExecutorStats& LastStats() const noexcept { return m_stats; }
    /// The last step's trace (empty unless ExecutorDesc::trace).
    [[nodiscard]] const std::vector<TraceRecord>& Trace() const noexcept { return m_trace; }
    /// Sounds requested by the last step.
    [[nodiscard]] const std::vector<SoundRequest>& Sounds() const noexcept { return m_sounds; }
    /// Prefabs spawned by the last step, in effect order.
    [[nodiscard]] const std::vector<SpawnRequest>& Spawns() const noexcept { return m_spawns; }
    /// Continuations waiting for a later step.
    [[nodiscard]] std::size_t PendingContinuations() const noexcept;

private:
    friend class ApplyContext;

    static constexpr std::string_view kInputActionTrigger = "trigger.InputAction";

    struct Instance;
    struct Firing;
    struct Continuation;
    struct Raised;

    void Recompile();
    void CollectWave0(std::vector<Firing>& firings);
    /// `only`: just that chain (Start).
    void AddTriggerFirings(std::vector<Firing>& firings, std::string_view trigger, std::uint64_t entity,
                           std::string_view key, const std::vector<values::Value>& payload,
                           std::optional<ChainId> only = std::nullopt);
    void SortFirings(std::vector<Firing>& firings) const;
    [[nodiscard]] bool HasAuthority(Authority authority, std::uint64_t entity) const;
    void AddTrace(TraceRecord record);
    void RejectWrite(const std::string& message);

    const ChainRegistry* m_registry;
    const values::ValueRegistry* m_valueRegistry;
    ExecutorDesc m_desc;
    RuntimeValues m_values;
    std::vector<std::unique_ptr<Instance>> m_instances; ///< By ChainId; nullptr once removed.
    std::vector<Raised> m_raised;
    std::vector<Continuation> m_continuations;
    std::vector<TraceRecord> m_trace;
    std::vector<SoundRequest> m_sounds;
    std::vector<SpawnRequest> m_spawns;
    std::uint64_t m_nextSpawnId = 0;
    std::vector<sdk::Subscription> m_subscriptions;
    ExecutorStats m_stats;
    std::uint64_t m_step = 0;
    std::uint64_t m_sequence = 0; ///< Orders firings within a step and continuations across steps.
    bool m_dirty = false;
};

} // namespace interi::chain
