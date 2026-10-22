# Concurrency & Job System — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. CONC-006: make TSan a blocking check first, so every later fix is verified under TSan in CI before merge.
2. CONC-001: the only High item. It is a crash on the hot-reload and shutdown path and needs a small API decision.
3. CONC-004, CONC-005, CONC-010: cheap debug assertions that turn silent races and deadlocks into immediate failures (one pattern: an owner or worker check like `CommandListPool`).
4. CONC-002, CONC-012: bound recursion and stack depth (they interact).
5. CONC-009: make the Jolt adapter's retry loop help instead of yield (module repo; needs a "run one job" API, which also serves CONC-003).
6. CONC-003, CONC-013: waiting policy (blocking waits, scoped helping).
7. CONC-007, CONC-015: contention and allocation work, measured with the existing benchmarks.
8. CONC-008: frame pipelining. This is the largest change and benefits from all of the above.
9. CONC-011, CONC-014: hygiene.

CONC-016, CONC-017 and CONC-018 are Info items: document them only (see the notes at the end).

## CONC-001 — Jobs submitted by a module can run after the module's library is closed
- **Severity / Effort:** High / M
- **Depends on:** None (it pairs with a Hot-Reload/Module-SDK decision)
- **Steps:**
  1. Give modules a scoped job handle instead of the raw `JobSystem&`. Add a `ModuleJobs` (or `JobScope`) object owned by `ModuleHost::Loaded`. It wraps the host `JobSystem` and counts every job submitted through it with its own `JobCounter`. Expose it through `IEditorDocument::Jobs()` and the registrar (bump `INTERI_MODULE_ABI_VERSION`).
  2. In `Loaded::~Loaded()` (`engine/hotreload/src/ModuleHost.cpp:69-84`), call `jobs.WaitFor(scopeCounter)` **before** `scheduler.reset()` and `library.Close()`. The same wait must happen in `Reload` before the outgoing instance's library closes (step 5 of the transaction).
  3. Fix destruction order in the owners. In `GameSession` (`GameSession.h:95-101`), either declare `m_jobs` after `m_modules`/`m_scene` or explicitly reset `m_modules` only after draining. In `tools/editor/main.cpp:155-156`, call `app.Shutdown()` (unload modules) only after quiescing jobs, or construct the `JobSystem` after the app's module host and destroy it before. The invariant to state is: **no queued job references module code when its library closes.**
  4. (Debug) Optionally record the submitting module's id in the job header when asserts are on, and assert in `Loaded::~Loaded` that the scope counter is zero after the wait.
- **Illustrative change (not applied):**
  ```cpp
  // ModuleHost.cpp, Loaded::~Loaded()
  if (jobs != nullptr) { jobs->WaitFor(moduleJobs); } // module code may still be queued
  scheduler.reset();
  ...
  library.Close();
  ```
- **Verification:** Add a hot-reload test module that submits 1,000 Low-priority jobs that sleep 1 ms and does not wait for them, then `Reload` and unload it. It must pass under ASan and TSan in `ModuleHostTest`. Add a GameSession test that destroys a session while such jobs are queued.
- **Risks / trade-offs:** Unload now blocks until the module's jobs finish, so long cooking jobs delay a reload. Modules should support cancellation (a stop flag checked by long jobs). An alternative, executing jobs only through engine-side thunks, does not help, because the stored callable is still module code.

## CONC-002 — Pool exhaustion runs jobs inline: priority ignored, unbounded recursion
- **Severity / Effort:** Medium / M
- **Depends on:** CONC-012
- **Steps:**
  1. Count exhaustion: add an atomic `m_inlineRuns` with a `Stats()` accessor and log a one-time warning per system.
  2. Never run a Low job inline on exhaustion while workers exist (AC-2). Let the caller help (run one High/Normal job through `FindJob`, bounded by `waitDepth`) and retry the allocation; if the pool is still full, queue the job without the pool (a heap-allocated overflow `Job` on a per-priority overflow list that workers drain), never inline. Inline execution stays only for a system with zero workers.
  3. In `TaskGraph::SubmitNode`, avoid recursion: the finishing job collects ready successors into a local worklist and submits them iteratively, and exhaustion takes step 2's non-inline fallback, so a successor (Low ones included) never runs inline on the finishing thread. A cleaner fix is to give `TaskGraph` preallocated per-node job storage, since the graph is static, so it never touches the shared pool.
  4. Size `maxJobs` from the peak observed in the stress and benchmark runs, and document the sizing.
- **Illustrative change (not applied):**
  ```cpp
  // TaskGraph job body: iterative release of successors
  SmallVector<NodeId, 8> ready;
  for (NodeId s : m_nodes[node].successors)
      if (m_remaining[s].fetch_sub(1, std::memory_order_acq_rel) == 1) ready.push_back(s);
  for (NodeId s : ready) SubmitNode(jobs, counter, s); // queues (pool or overflow), never runs inline
  ```
- **Verification:** A stress test with `maxJobs = 4` and a 10,000-node chain TaskGraph must complete on the Windows MSVC debug preset (1 MB stack), and a test must assert that Low jobs are never executed on the submitting thread while workers exist.
- **Risks / trade-offs:** Helping on exhaustion can itself nest. Bound it with the same `waitDepth` counter.

## CONC-003 — `WaitFor` never blocks: a long wait burns a full core
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Add a blocking phase to `WaitFor`. After the pause and yield budget (or immediately for non-worker threads waiting on a counter with Low work), park on a per-counter or per-system wait primitive. C++20 `std::atomic<int64_t>::wait/notify_all` on `m_pending` is the simplest: `Execute` calls `notify_all` when the decrement reaches 0.
  2. Keep helping first. Park only when `FindJob` returns nothing for the waiter's allowed priorities.
  3. Give `AsyncFileReader::WaitAll` a blocking wait, because it can never help with Low IO while workers exist.
- **Illustrative change (not applied):**
  ```cpp
  // Execute(): if (counter->m_pending.fetch_sub(1, acq_rel) == 1) counter->m_pending.notify_all();
  // WaitFor(): after kPausesBeforeYield idle polls:
  //   if (auto v = counter.m_pending.load(acquire); v != 0) counter.m_pending.wait(v, acquire);
  ```
  Waiting on a non-zero value only parks until the next change, so re-check in the loop. Jobs that become available while the waiter is parked do not wake it; that is acceptable for non-worker waiters on Low work, and helpers should time-box the wait instead.
- **Verification:** A test where a non-worker thread calls `AsyncFileReader::WaitAll` on a 200 ms slow `IFileSystem`, measuring process CPU time: the waiter accounts for less than 5% of one core. All job stress tests stay green under TSan.
- **Risks / trade-offs:** `notify_all` on every counter reaching zero adds a syscall only when someone waits (libstdc++ and MSVC keep a waiter count). A parked waiter that could have helped reduces throughput, so park only after help fails.

## CONC-004 — Unenforced "never wait on Low work from inside a job" contract
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `AsyncFileReader::WaitAll` and `~AsyncFileReader`, add `INTERI_ASSERT(!m_jobs.CurrentWorkerIndex().has_value() || m_jobs.WorkerCount() == 0, "AsyncFileReader::WaitAll from inside a job")`.
  2. Optionally add a `JobSystem::IsInsideJob()` (a non-zero `waitDepth` or an executing flag in `ThreadState`) so the main thread is also caught while it runs a job inside `WaitFor`.
  3. Mirror the check in any future API that waits on Low work.
- **Verification:** A death or `ScopedAssertHandler` test that calls `WaitAll` from inside a job and expects the assertion. It skips when `!kAssertsEnabled`.
- **Risks / trade-offs:** None. Shipping builds are unaffected.

## CONC-005 — `CommandBuffers::Local` silently shares one buffer between threads
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `CommandBuffers::Local`, assert that a worker's index fits (`*worker + 1 < m_buffers.size()`) instead of falling through.
  2. For the non-worker slot, record the owning `std::thread::id` on first use per playback cycle (or at construction, as `CommandListPool` does) and assert that the same thread records until `Playback`.
  3. Store the `JobSystem*` the buffers were sized for and assert that `Local(jobs)` receives the same one.
- **Illustrative change (not applied):**
  ```cpp
  INTERI_ASSERT(&jobs == m_jobs, "CommandBuffers used with a different JobSystem");
  if (auto w = jobs.CurrentWorkerIndex()) { INTERI_ASSERT(*w + 1 < m_buffers.size()); return *m_buffers[*w]; }
  INTERI_ASSERT(CheckNonWorkerOwner(), "two non-worker threads record into CommandBuffers");
  return *m_buffers.back();
  ```
- **Verification:** Unit tests with a mismatched `slotCount` and with two `std::thread`s recording must hit the assertion. The existing determinism tests at 1/4/32 threads stay green.
- **Risks / trade-offs:** A single non-worker owner must be reset in `Playback` if different threads legitimately run successive frames.

## CONC-006 — ThreadSanitizer is not a PR or push gate
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add `linux-clang-tsan` to the `linux` job matrix in `.github/workflows/ci.yml:48`, restricted to `-L "unit|stress"` to bound time. Alternatively, add a separate `tsan` job on `pull_request` running the stress label plus `ModuleHostTest`/`ModuleProjectTest`.
  2. Keep the full TSan and ASan run nightly.
  3. Mark the TSan PR job as a required status check on `main`.
- **Verification:** Open a PR. The TSan job appears and blocks merge on failure. A deliberately racy test in a scratch branch fails it.
- **Risks / trade-offs:** Longer CI time and TSan-instrumented vcpkg builds. The cache key already includes the triplets, so after the first build the cost is mainly test time.

## CONC-007 — Every job does several RMWs on a few global cache lines
- **Severity / Effort:** Medium / L
- **Depends on:** CONC-006 (TSan gate while touching lock-free code)
- **Steps:**
  1. Measure first: run `JobSystem_SmallJobsScaling` and `JobSystem_NestedSubmit` (`benchmarks/jobs/JobBenchmarks.cpp`) with the release preset at 1/4/16/32 workers and record jobs per second.
  2. Add per-thread job free lists in `ThreadState` (magazine/cache of e.g. 64 jobs) that refill from and spill to the global `PoolAllocator` in batches. `Execute` frees to the executing thread's cache.
  3. Drop `m_freeCount` RMWs from the hot path. Keep it only behind a debug flag, or compute it on demand. At least move it to its own cache line, away from `m_head`.
  4. Consider batching `m_queued` updates for `ParallelForChunks` helpers (one `fetch_add(n)` for n helpers), and use relaxed or acquire orders where the Dekker argument does not need seq_cst (document each).
- **Verification:** Benchmark throughput at 16 and 32 workers improves measurably (target at least 1.5x for 64-byte jobs). All job stress tests remain clean under TSan.
- **Risks / trade-offs:** Per-thread caches strand blocks on idle threads. Cap the cache size and spill on sleep. Changing memory orders on `m_queued` risks lost wake-ups, so keep seq_cst on the Dekker pair.

## CONC-008 — Frame pipelining exists but no frame loop uses it
- **Severity / Effort:** Medium / XL
- **Depends on:** CONC-001, CONC-003, CONC-013
- **Steps:**
  1. Define the render snapshot the game produces each step: transforms, visible meshes, decals, particles, camera. It is immutable after publication.
  2. Drive `GameSession`'s host loop (and the editor's play mode) through `FramePipeline<RenderSnapshot>`: simulate N+1 on the main thread while render prep of N (culling lists, upload staging, command recording through `CommandListPool::Acquire` on workers) runs as High jobs.
  3. Split `EditorRenderer` recording into per-pass jobs, using `CommandListPool` per-worker lists and submitting in a fixed order.
  4. Document the thread model (see Goal.md) next to `FramePipeline`.
- **Verification:** A runtime stress test (already present for `FramePipeline`) extended to a real `GameSession` under TSan. Tracy captures on the profile preset show sim and render prep overlapping, and CPU frame time falls by roughly the render-prep share.
- **Risks / trade-offs:** One frame of added latency. Snapshot copy cost. The editor's immediate-mode UI makes full pipelining harder, so start with the game path.

## CONC-009 — Jolt adapter spins without helping when its job pool runs dry
- **Severity / Effort:** Medium / S (module repository)
- **Depends on:** a public "help once" API in `JobSystem` (also useful for CONC-002 and CONC-003)
- **Steps:**
  1. Add `bool JobSystem::RunOneJob()` (or `HelpOnce()`) that executes a single High/Normal job found via `FindJob` and returns whether it did anything.
  2. In `modules/physics/src/JoltJobSystem.cpp:35-41`, replace `std::this_thread::yield()` with `if (!m_jobs.RunOneJob()) std::this_thread::yield();`.
  3. Log a warning the first time the pool runs dry, so `maxJobs` gets raised.
- **Verification:** A module stress test with `maxJobs` set to a small number (e.g. 16) and many bodies across all workers completes without hanging, under TSan.
- **Risks / trade-offs:** Helping from inside Jolt's `CreateJob` runs unrelated jobs on Jolt's stack, which adds nesting depth. Bound it with `kMaxWaitNesting`.

## CONC-010 — `AudioEngine` single-game-thread contract is not asserted
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Store `std::this_thread::get_id()` in `AudioEngine::Create`, or on the first call.
  2. Assert it in `Play`, `Stop`, `SetPosition`, `SetVolume`, `SetListener`, `SetMasterVolume`, `Update` and `Render`.
- **Verification:** A test calling `Play` from a `std::thread` hits the assertion (skipped without asserts). `AudioEngineStress` stays green.
- **Risks / trade-offs:** If a game legitimately moves its game loop to another thread, provide a `RebindOwnerThread()`.

## CONC-011 — Global logger is a raw pointer with an unprotected lifetime
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `tools/editor/main.cpp`, use an RAII guard (`ScopedGlobalLogger`) so every return path uninstalls the logger before it is destroyed.
  2. Document in `Log.h` that uninstalling must happen after all threads that may log have stopped. Alternatively, have `~Logger` assert `GetGlobalLogger() != this`.
- **Illustrative change (not applied):**
  ```cpp
  struct ScopedGlobalLogger { explicit ScopedGlobalLogger(Logger& l) { SetGlobalLogger(&l); }
                              ~ScopedGlobalLogger() { SetGlobalLogger(nullptr); } };
  ```
- **Verification:** An ASan run of `level_editor --video-driver invalid` (failure path) shows no use-after-free. A unit test checks that the assertion in `~Logger` fires when the logger is still installed.
- **Risks / trade-offs:** None.

## CONC-012 — Worker stack size is fixed by `std::thread` while waits nest 32 jobs deep
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add `JobSystemDesc::workerStackBytes` (default 4 MB). Create workers with `pthread_attr_setstacksize` on POSIX and `_beginthreadex(..., stackSize, ...)` on Windows, or wrap them in a small platform thread class.
  2. Alternatively, or additionally, lower `kMaxWaitNesting` on Windows, or derive it from the stack size and a per-level budget.
  3. Add a stress test on MSVC debug: nested `ParallelFor` 32 levels deep, each job using 16 KB of stack.
- **Verification:** The new test passes on `windows-msvc-debug` in CI.
- **Risks / trade-offs:** More virtual memory reserved per worker (committed lazily). A platform-specific thread creation path.

## CONC-013 — Helping waits may pick unrelated long jobs
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Offer a "scoped help" mode in `WaitFor`: first drain the caller's own deque (where its children are), then steal only High jobs, then Normal jobs only after a bounded idle period. Alternatively, tag jobs with a group id and help only with jobs of the awaited group.
  2. Document the rule "long-running work is Low" in `JobSystem.h`, next to `JobPriority`, and audit submitters (`AsyncFileReader` already complies).
- **Verification:** A benchmark in which the main thread waits on a 1 ms `ParallelFor` while 50 ms Normal jobs are queued: the 99th-percentile wait time stays within about 2x of the ParallelFor time.
- **Risks / trade-offs:** Less helping means lower throughput when the waiter could have done useful work.

## CONC-014 — Worker threads are only named in profiling builds
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `platform::SetCurrentThreadName(std::string_view)` (`pthread_setname_np`, truncated to 15 characters, and `SetThreadDescription`) in `engine/core` or `engine/jobs`, and call it unconditionally in `WorkerMain` and the logger's flush thread.
  2. Keep the Tracy call under `INTERI_PROFILING_ACTIVE`.
- **Verification:** `top -H` or `ps -T` on `level_editor` shows "InteriWorker N" and "InteriLogFlush".
- **Risks / trade-offs:** None.

## CONC-015 — Heap allocation inside parallel chain waves
- **Severity / Effort:** Low / M
- **Depends on:** CONC-007 (allocator strategy)
- **Steps:**
  1. Give each wave evaluation a per-thread scratch arena. Wire the existing `FrameAllocators`, indexed by `CurrentWorkerIndex()` plus the waiter slot, into `Outcome::inputs` through `StlAllocator`.
  2. Reserve `outcomes[i].inputs` from the program's known input counts in a serial pass to avoid reallocation.
- **Verification:** The chain stress benchmark shows fewer `malloc` calls per step (count with a `TrackedAllocator` or heaptrack). `ChainDeterminism.MatchesCrossPlatformGoldenHash` is unchanged.
- **Risks / trade-offs:** Arena lifetime must cover the serial effect phase. Strings inside `Value` still allocate unless `Value` gets small-string storage.

## Info items (no remediation required)
- **CONC-016:** Add a comment in `JobSystem::Enqueue` stating that `m_queued` may be transiently negative and why the wake protocol tolerates it. Keep any future change to the order (publish, then count) under TSan review.
- **CONC-017:** Optionally static-assert or document the 32-bit ABA tag margin in `PoolAllocator.h`. No change needed.
- **CONC-018:** Preserve these verified properties. See the guardrails in Goal.md.
