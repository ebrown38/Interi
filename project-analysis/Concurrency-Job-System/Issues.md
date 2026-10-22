# Concurrency & Job System — Issues

**Project:** Interi (repository root, branch `autoflow/003-goal-implement-milestone-21-8367f0` @ 4631f35)
**Analyzed:** 2026-10-19
**Scope:** First-party concurrent code: `engine/jobs` (JobSystem, WorkStealingDeque, TaskGraph, ParallelAlgorithms), `engine/core` (MPMCQueue, SPSCQueue, PoolAllocator, FrameAllocators, Logger, StringId, ComponentRegistry), `engine/ecs` (Scheduler, Query::ParallelForEachChunk, CommandBuffers, World ticks), `engine/audio/src/AudioEngine.cpp`, `engine/runtime/FramePipeline.h`, `engine/platform/AsyncFileReader`, `engine/rhi/CommandListPool`, `engine/chain/src/Executor.cpp` (parallel waves), `engine/hotreload/ModuleHost.cpp` (unload order), `engine/game/GameSession`, `tools/editor/main.cpp`, `.github/workflows/ci.yml`, plus the unindexed module projects `modules/physics/src/JoltJobSystem.cpp` and the job usage in `modules/destruction/libs/destruction/src`. About 25 source files read in full or in part. Static reading only.
**Summary:** 18 issues — Critical: 0 · High: 1 · Medium: 8 · Low: 6 · Info: 3

### Thread model (as found)

| Thread | Created by | Count | Notes |
|---|---|---|---|
| Main / game thread | process | 1 | Constructs the `JobSystem`, so it owns the last deque set (`JobSystem.cpp:120-132`). Runs simulation, chain effects, rendering and command recording one after another. It helps in `WaitFor` (High/Normal only). |
| Job workers | `JobSystem` ctor (`std::thread`) | `hardware_concurrency()-1`, at least 1 (`JobSystem.cpp:103-106`) | Chase-Lev deque per priority, spin-then-sleep (Go/Tokio searching protocol). Optional pinning to CPU i+1. Named only in profiling builds. |
| Logger flush thread | `Logger` ctor (`std::jthread`) | 1 per Logger | Drains the MPSC ring every 5 ms and runs the sinks (Console, File, the editor's `LogBufferSink`). |
| Audio device thread | miniaudio (`ma_device_start`) | 1 (Default/Null backend) | Takes commands from an SPSC queue and returns ended voices through a second one. |
| (Module) Jolt physics | none of its own | 0 | `modules/physics` routes Jolt through `interi::JobSystem` via `JoltJobSystem` (High priority). |

No `std::async`, `detach()` or per-task thread creation exists in first-party engine, tool or sample code (literal search of `engine/`, `tools/` and `samples/`).

## Findings

### CONC-001 — Jobs submitted by a module can run after the module's library is closed (no quiescence on unload or reload)
- **Severity:** High
- **Confidence:** Medium (no guard exists, verified; no in-tree module triggers it today)
- **Category:** Lifetime / hot reload
- **Location:** `engine/jobs/include/interi/jobs/JobSystem.h:187-192`, `engine/hotreload/src/ModuleHost.cpp:69-84`, `engine/game/include/interi/game/GameSession.h:95-101`, `tools/editor/main.cpp:155-156`, `engine/editor_ui/include/interi/editor_ui/EditorExtensionsApi.h:85`
- **Evidence:** `Submit` is a header template. Its `job->invoke` thunk and the stored callable are therefore instantiated in **the binary that calls `Submit`**:
  ```cpp
  job->invoke = [](Job& self) noexcept { Function* stored = ...; (*stored)(); std::destroy_at(stored); };
  ```
  When a gameplay module or editor extension submits a job, the queued `Job` holds a function pointer into the module's shadow-copied DLL. Modules reach the host's `JobSystem` through `IEditorDocument::Jobs()`, and the physics module holds one in `JoltJobSystem`. `Loaded::~Loaded()` destroys the scheduler and then calls `library.Close()` (`ModuleHost.cpp:73-84`). Nothing on that path waits for, or rejects, jobs the module still has queued. `ModuleHost` only sees a `JobSystem` inside `RunSystems`, and `Reload(id)` has none at all. Member order makes this worse at shutdown. `GameSession` declares `m_jobs` (line 95) before `m_modules` (line 99), and the editor constructs `JobSystem jobs` before `EditorApp app` (`main.cpp:155-156`). In both cases the modules unload **first**, and the `JobSystem` destructor then drains every queued job (`JobSystem.cpp:162-166`), including any whose code was just unmapped. The CLAUDE.md rule "Never leave the host anything from module code that outlives the module" covers this case, but it is enforced only for systems, hooks and savers. Today the only module submitter, `JoltJobSystem`, protects itself (`~JoltJobSystem` waits on `m_outstanding`, `modules/physics/src/JoltJobSystem.cpp:17-20`).
- **Impact:** A single fire-and-forget or Low-priority job from a module (an async cook, thumbnail or IO job) that is still queued during a hot reload, a project switch or shutdown executes unmapped code (SIGSEGV), or worse, code from the newly loaded copy at the same address. The trigger is timing-dependent, so tests rarely hit it, and TSan does not detect it.

### CONC-002 — Pool exhaustion runs jobs inline: priority ignored, unbounded recursion in TaskGraph
- **Severity:** Medium
- **Confidence:** High
- **Category:** Job system design / stack depth
- **Location:** `engine/jobs/include/interi/jobs/JobSystem.h:172-177`, `engine/jobs/src/TaskGraph.cpp:83-98`, `engine/platform/src/AsyncFileReader.cpp:32`
- **Evidence:** `Submit` calls `function()` directly when `AllocateJob()` returns null (`maxJobs` defaults to 16 Ki). `TaskGraph::SubmitNode` submits successors from inside the finishing node. Under exhaustion each successor therefore runs **nested** in the predecessor's frame, so stack depth grows with the length of the dependency chain. The inline path also discards `priority`. A Low IO job from `AsyncFileReader::Request` runs synchronously on the calling (main) thread, and a High render-prep job runs before the caller's own work.
- **Impact:** Bursts such as destruction fracture, many chain activations or large `ParallelFor` fan-outs from many threads can overflow the 1 MB Windows thread stack in a deep TaskGraph, and IO can stall the frame thread unexpectedly. Exhaustion is silent: there is no counter or log entry.

### CONC-003 — `WaitFor` never blocks: a long wait burns a full core
- **Severity:** Medium
- **Confidence:** High
- **Category:** Waiting / power and oversubscription
- **Location:** `engine/jobs/src/JobSystem.cpp:300-329`, `engine/platform/src/AsyncFileReader.cpp:15-18`, `engine/platform/src/AsyncFileReader.cpp:68-71`
- **Evidence:** When `FindJob` returns nothing, the waiter loops `CpuPause()` 64 times and then `std::this_thread::yield()` forever. It never sleeps on a condition variable. `AsyncFileReader::WaitAll` and its destructor wait on Low-priority IO jobs, which the waiter itself is not allowed to run while workers exist (`priorityCount = kJobPriorityCount - 1`, line 310). It therefore spins and yields for the entire duration of the disk IO.
- **Impact:** A main thread waiting for IO, or for a long job running on another worker, consumes 100% of one core and competes with the workers that do the actual work (`yield` hands the slice to any runnable thread, often nobody useful). On laptops and consoles this costs power and thermal headroom. In the editor it shows as a pegged CPU during loads.

### CONC-004 — Unenforced "never wait on Low work from inside a job" contract (deadlock-prone)
- **Severity:** Medium
- **Confidence:** High
- **Category:** Deadlock
- **Location:** `engine/platform/include/interi/platform/AsyncFileReader.h:22-25`, `engine/platform/src/AsyncFileReader.cpp:15-18`, `engine/platform/src/AsyncFileReader.cpp:68-71`, `engine/jobs/src/JobSystem.cpp:308-310`
- **Evidence:** The header documents that `WaitAll` and the destructor "must run on a thread that is not executing a job", because a waiting worker skips Low jobs. Neither function asserts this (`jobs.CurrentWorkerIndex()` is available and is what `CommandListPool` uses for the same kind of check). An `AsyncFileReader` owned by an object that a job destroys, or a module system that calls `WaitAll`, can hang the process once every worker is inside such a wait.
- **Impact:** A hard hang under load that is invisible in tests with idle workers. The rule lives only in documentation.

### CONC-005 — `CommandBuffers::Local` silently shares one buffer between threads
- **Severity:** Medium
- **Confidence:** High
- **Category:** Data race (API misuse not detected)
- **Location:** `engine/ecs/src/CommandBuffer.cpp:83-91`, `engine/ecs/include/interi/ecs/CommandBuffer.h:113-116`
- **Evidence:**
  ```cpp
  if (worker.has_value() && *worker + 1 < m_buffers.size()) return *m_buffers[*worker];
  return *m_buffers.back();
  ```
  Every non-worker thread, and any worker whose index does not fit (a `CommandBuffers` built with an explicit smaller `slotCount`, or with a different `JobSystem` than the one passed to `Local`), falls through to the **same** last buffer. Recording there is plain `std::vector::push_back` plus a block allocator. Nothing asserts, unlike `CommandListPool::Acquire`, which asserts on the owner thread (`engine/rhi/src/CommandListPool.cpp:97-101`).
- **Impact:** The fallback converts a configuration mistake (a second game thread recording, a mismatched job system, a hot-reloaded module that passes another `JobSystem`) into heap corruption in ECS structural changes, and it reproduces only nondeterministically.

### CONC-006 — ThreadSanitizer is not a PR or push gate
- **Severity:** Medium
- **Confidence:** High
- **Category:** Testing / CI
- **Location:** `.github/workflows/ci.yml:48`, `.github/workflows/ci.yml:145-152`
- **Evidence:** The `linux` job that runs on push and pull requests uses the presets `[linux-clang-debug, linux-clang-release, linux-clang-asan, linux-clang-tidy, linux-clang-profile]`. `linux-clang-tsan` runs only in `nightly`, which triggers on `schedule` and `workflow_dispatch`. The stress suites (`JobSystemStress`, `WorkStealingDequeStress`, `MPMCQueueStress`, `PoolAllocatorStress`, `LoggerStress`, `AudioEngineStress`, `ModuleHostTest`) therefore run under TSan only after the merge.
- **Impact:** A race introduced in a PR reaches `main` and is reported the next morning. The local "run TSan before committing" rule in CLAUDE.md is not enforced.

### CONC-007 — Every job does several RMWs on a few global cache lines
- **Severity:** Medium
- **Confidence:** Medium (inferred from code; benchmarks not run)
- **Category:** Contention / scalability
- **Location:** `engine/jobs/src/JobSystem.cpp:193-196`, `engine/jobs/src/JobSystem.cpp:227-228`, `engine/jobs/src/JobSystem.cpp:250`, `engine/jobs/src/JobSystem.cpp:293`, `engine/core/src/memory/PoolAllocator.cpp:57-90`, `engine/core/include/interi/core/memory/PoolAllocator.h:55-56`
- **Evidence:** For each job, `Submit` CASes the pool's single `m_head` and does `m_freeCount.fetch_sub`, then a seq_cst `m_queued.fetch_add` plus a load of `m_searching`. `FindJob` does `m_queued.fetch_sub`. `Execute` frees the block (another `m_head` CAS and `m_freeCount.fetch_add`). That is about six atomic RMWs per job, all against three global cache lines shared by every thread. `m_head` and `m_freeCount` also sit on the same line. `m_freeCount` exists only for diagnostics. The work-stealing deques are per-thread, but job storage is not.
- **Impact:** With many small jobs (`ParallelForEachChunk` with grain 1, chain waves, Jolt's fine-grained jobs) at high thread counts (16 to 32), throughput is bounded by cache-line ping-pong on the pool head and `m_queued` instead of by the deques. This affects destruction bursts in particular, where many small jobs arrive at once.

### CONC-008 — Frame pipelining exists but no frame loop uses it; the frame is serial
- **Severity:** Medium
- **Confidence:** High
- **Category:** Frame pipeline / thread architecture
- **Location:** `engine/runtime/include/interi/runtime/FramePipeline.h:19-45`, `engine/game/src/GameSession.cpp:92`, `tools/editor/EditorRenderer.cpp:377`, `tools/editor/EditorRenderer.cpp:440`
- **Evidence:** A literal search finds `FramePipeline` only in its header and `engine/runtime/src/HeaderCheck.cpp`. `GameSession::Step` runs module systems and then the scene on the caller, and the caller renders afterwards. The editor records its frame into one command list acquired on the main thread (`commandPools->Acquire()`), even though `CommandListPool` already supports per-worker pools.
- **Impact:** Simulation of frame N+1 never overlaps render preparation or recording of frame N. CPU frame time is sim + render prep + recording, with only the inner `ParallelFor` loops parallel. This is the main obstacle to absorbing destruction-heavy frames within budget.

### CONC-009 — Jolt adapter spins without helping when its job pool runs dry (possible livelock)
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Deadlock / livelock (module project)
- **Location:** `modules/physics/src/JoltJobSystem.cpp:30-43`, `modules/physics/src/JoltJobSystem.cpp:51-64`
- **Evidence:** When `m_joltJobs.ConstructObject` fails, `CreateJob` loops `std::this_thread::yield()` until a Jolt job is freed. A Jolt job is freed only after its wrapping **Interi** job has run `job->Release()`. Jolt calls `CreateJob` from inside jobs that are themselves running on Interi workers. If every worker (plus the main thread inside the physics update) is in this loop, the queued Interi jobs that hold the references never run, and no thread makes progress. Interi exposes no "run one job" call the loop could use. The assert covers only the zero-worker case.
- **Impact:** A physics step with an undersized `maxJobs` can hang under load (many islands, destruction debris) instead of degrading.

### CONC-010 — `AudioEngine` single-game-thread contract is not asserted
- **Severity:** Low
- **Confidence:** High
- **Category:** Thread-safety contract
- **Location:** `engine/audio/include/interi/audio/AudioEngine.h:68`, `engine/audio/src/AudioEngine.cpp:248-301`, `engine/audio/src/AudioEngine.cpp:353-380`
- **Evidence:** "Every member function is called from one game thread". `Play`, `Stop` and `Update` are the single producer of an `SPSCQueue` and mutate `voices` and `retired` without locks. No owner-thread assertion exists. Chain `PlaySound` effects are host requests today, but a module system running on a worker that calls `Play` would silently break the SPSC invariant.
- **Impact:** A misuse would cause lost commands, a corrupted ring or use-after-free of clips, with no diagnostic in debug builds.

### CONC-011 — Global logger is a raw pointer with an unprotected lifetime
- **Severity:** Low
- **Confidence:** High
- **Category:** Lifetime
- **Location:** `engine/core/include/interi/core/Log.h:145-160`, `tools/editor/main.cpp:131-140`, `tools/editor/main.cpp:219`
- **Evidence:** `LogFormat` loads the global `Logger*` and then calls `logger->Log`. Nothing stops another thread from uninstalling and destroying it in between. In the editor's `main`, the early `return EXIT_FAILURE` paths (lines 137-140 and 146-150) leave `&logger` installed while the stack `Logger` is destroyed, so any later log from a static destructor or a still-running thread dereferences freed memory.
- **Impact:** A rare use-after-free at shutdown and on failure paths.

### CONC-012 — Worker stack size is fixed by `std::thread` while waits nest 32 jobs deep
- **Severity:** Low
- **Confidence:** Low (inferred; no stack measurement)
- **Category:** Stack depth / Windows
- **Location:** `engine/jobs/src/JobSystem.cpp:136-147`, `engine/jobs/include/interi/jobs/JobSystem.h:83-84`, `engine/jobs/include/interi/jobs/JobSystem.h:92`
- **Evidence:** Workers are `std::thread`s, which get 1 MB on Windows and no way to set a size. `WaitFor` may run another job on the same stack up to `kMaxWaitNesting = 32` levels, which leaves an average budget of about 32 KB per nested job frame. `LogFormat` alone places a 400-byte buffer on the stack, and solver jobs (StressSolver, Jolt) are larger.
- **Impact:** Stack overflow on Windows under deep nesting, combined with the inline recursion in CONC-002. It is hard to reproduce on Linux, where the default stack is 8 MB.

### CONC-013 — Helping waits may pick unrelated long jobs (latency inversion)
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Scheduling / frame latency
- **Location:** `engine/jobs/src/JobSystem.cpp:312-317`, `engine/jobs/src/JobSystem.cpp:257-298`
- **Evidence:** Below the nesting cap, `WaitFor` runs any High or Normal job it can find: its own deque, the injection queue, or a steal from anyone. A main thread waiting on a short `ParallelFor` (or on `FramePipeline`'s render prep) can steal and run an unrelated long Normal job and finish it before returning.
- **Impact:** Frame-time spikes that depend on what else is queued. Long work must be submitted as Low to avoid this, and that rule appears only in comments.

### CONC-014 — Worker threads are only named in profiling builds
- **Severity:** Low
- **Confidence:** High
- **Category:** Tooling
- **Location:** `engine/jobs/src/JobSystem.cpp:338-341`, `engine/core/include/interi/core/Profiling.h:42`
- **Evidence:** The name is set only under `INTERI_PROFILING_ACTIVE`, through Tracy's `SetThreadName`. No `pthread_setname_np` or `SetThreadDescription` is called, and neither the logger flush thread nor any other thread is named.
- **Impact:** Debuggers, `perf`, `top -H`, crash dumps and TSan reports show anonymous threads in debug and release builds.

### CONC-015 — Heap allocation inside parallel chain waves
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Allocation in jobs
- **Location:** `engine/chain/src/Executor.cpp:685-715`
- **Evidence:** Each activation evaluated in the `ParallelFor` does `outcome.inputs.reserve(...)` and `push_back(EvaluateExpression(...))` (each `Value` may hold a `std::string` or a list) and records effects (type-erased callables). All of this uses the global heap from every worker. The engine's `FrameAllocators` (per-thread, lock-free) exist but have no users (literal search).
- **Impact:** Global `malloc` contention when many chains fire in one step (a trigger storm during a destruction event), which reduces the speed-up of the wave.

### CONC-016 — `m_queued` can be transiently negative
- **Severity:** Info
- **Confidence:** High
- **Category:** Memory ordering (documentation)
- **Location:** `engine/jobs/src/JobSystem.cpp:198-232`, `engine/jobs/src/JobSystem.cpp:291-294`
- **Evidence:** `Enqueue` publishes the job to the deque or injection queue **before** `m_queued.fetch_add`, so a thief can `fetch_sub` first. The searching and wake heuristics (`waiting > m_searching`, `m_queued > 0`) then briefly under-count. Reasoning through the protocol, the submitter's later increment and the sleep predicate's re-check under the mutex still prevent a lost wake-up. The invariant is subtle and undocumented.
- **Impact:** None found, but a future change to the wake rule could turn it into a lost wake-up. It should be written down next to the Dekker comment.

### CONC-017 — PoolAllocator ABA tag is 32 bits
- **Severity:** Info
- **Confidence:** High
- **Category:** Lock-free correctness
- **Location:** `engine/core/src/memory/PoolAllocator.cpp:13-24`, `engine/core/src/memory/PoolAllocator.cpp:57-73`
- **Evidence:** The head packs a 32-bit version and a 32-bit index. ABA requires a thread to stall between its load and its CAS while exactly 2^32 other pops and pushes occur on the same head. Next-links live outside the blocks, and the acquire/release pairing on `m_head` is correct.
- **Impact:** Theoretical only. Recorded so the margin is a conscious choice.

### CONC-018 — Verified correct (no action)
- **Severity:** Info
- **Confidence:** High
- **Category:** Verification record
- **Location:** `engine/jobs/include/interi/jobs/WorkStealingDeque.h:64-119`, `engine/core/include/interi/core/containers/MPMCQueue.h:70-128`, `engine/core/include/interi/core/containers/SPSCQueue.h:60-94`, `engine/core/src/Log.cpp:233-277`, `engine/jobs/src/JobSystem.cpp:234-255`, `engine/jobs/src/JobSystem.cpp:381-391`, `engine/jobs/src/TaskGraph.cpp:91-95`, `engine/ecs/src/Component.cpp:41-61`, `engine/audio/src/AudioEngine.cpp:97-119`, `engine/audio/src/AudioEngine.cpp:236-246`
- **Evidence:** Chase-Lev: a fixed buffer that never grows (so no reclamation race), seq_cst Pop/Steal Dekker, and a last-element CAS. The Vyukov MPMC queue and the Logger ring use correct sequence protocols, and the SPSC queue has correct release/acquire with its producer and consumer fields on separate cache lines. The sleep path is correct: `m_sleepers++` then a predicate re-check of `m_queued` under the mutex, paired with seq_cst `fetch_add` then `m_sleepers` load. `Execute` frees the job before the counter release. The TaskGraph `acq_rel` predecessor count is correct. Component registration is mutex-guarded with a release publish. Audio clips are freed only on the game thread, and the device stops before `Impl` is destroyed. Destruction (`StressSolver`, `Islands`, `Voronoi`, `DestructibleAsset`) uses `ParallelFor` and `SolveWithBudget`, and Jolt is routed through `interi::JobSystem` rather than its own thread pool.
- **Impact:** None. These are the properties the guardrails in Goal.md should preserve.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all third-party (SDL Joystick/Hidapi/Video, Tracy, Manifold, ImGui docking); no first-party cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL example/back-end flows under `external/vcpkg/buildtrees/sdl3`; none first-party |
| 4 | query | "job system worker thread work stealing" | `JobSystem` class (JobSystem.h:88), `ThreadState` (JobSystem.cpp:36), `~JobSystem`, `CommandBuffers::Local(JobSystem)`, scheduler tests, Jolt `JobSystemThreadPool` (vendored) |
| 5 | query | "mutex atomic lock-free queue" | Only third-party hits (Tracy moodycamel queue, SDL mutex/FIFO tests); BM25 index weak as warned |
| 6 | query | "parallel for chunks" | ECS `ChunkView`/`Query`/`World` chunk APIs, `UploadQueue::NextChunk`; no ParallelFor symbol ranked |
| 7 | context | `JobSystem` (Class, JobSystem.h) | 16 methods, 13 properties (`m_queued`, `m_searching`, `m_sleepers`, `m_jobPool`, `m_injection`...); typed holders: Scheduler RunState/SystemContext, GameSession, AsyncFileReader, CommandListPool, FramePipeline, SceneRuntime, EditorApp, DocumentHost |
| 8 | cypher | engine files matching jobs/Queue/Deque/Logger/Pool/Frame/Audio/AsyncFileReader/Watcher | 23 first-party files; drove the reading list |
| 9 | impact | `WaitFor` upstream (summaryOnly) | risk UNKNOWN, 0 resolved callers (member calls through references not resolved); confirmed by text search instead |
| 10 | cypher | CALLS into WaitFor/Submit/ParallelFor* from `engine/` | `chain::Executor::Step` → ParallelFor; ParallelAlgorithms internals → ParallelForChunks; member-call edges to Submit/WaitFor missing from the index |

Follow-up discovery after the graph gaps (#9, #10) used literal searches for `std::thread|std::async|detach|thread_local|std::mutex|condition_variable|.Submit(|FramePipeline|CommandListPool|FrameAllocators|AsyncFileReader|TaskGraph`, plus reads of the cited lines.

## Analysis limitations
- Static reading only. TSan, the stress suites and the job benchmarks were not run, so the contention claims (CONC-007) and the stack-depth claims (CONC-012) are inferred, not measured.
- GitNexus clusters and processes are dominated by third-party build-tree code, and the BM25 index failed to build. Member-call edges (`jobs.Submit`, `jobs.WaitFor`) are not resolved, so caller sets came from literal search, not the graph.
- `modules/physics` and `modules/destruction` are not indexed and are separate nested repositories. They were read only around their job-system integration (CONC-009, and CONC-018's notes). Jolt's internal barrier and threading code (vendored) was not audited.
- Windows-specific behaviour (MSVC `std::thread` stack size, `SetThreadAffinityMask` beyond 64 CPUs) is reasoned from documentation, not tested.

## Cross-discipline notes
- Determinism-Replication: `World::BeginRun()` (`engine/ecs/include/interi/ecs/World.h:152`) hands out ticks in run-start order of concurrently running systems, and `ComponentId<T>()` ids depend on which job registers first. Both are documented as not affecting layout or results, which is worth one more check.
- Hot-Reload / Module SDK: `ModuleHost` has no handle to the job system during `Reload`/`Unload` (`engine/hotreload/src/ModuleHost.cpp:69-84`). This needs an API decision (see CONC-001).
- Performance-Optimization: the editor console copies up to 2,000 log records (strings) under the `LogBuffer` mutex every frame (`engine/editor_ui/src/ServicePanels.cpp:35`, `engine/editor/src/EditorServices.cpp:190`), which also blocks the logger flush thread.
- Audio: if a `Start` command cannot be queued after an eviction, the evicted voice keeps playing while its slot is released (`engine/audio/src/AudioEngine.cpp:276-298`). The voice cannot be stopped from the game side.
