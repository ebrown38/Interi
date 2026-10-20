# Rendering-Pipeline — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

Project rules apply to every step: run `impact` before editing a symbol, and when adding an `IDevice`/`ICommandList` virtual, also add a `MOCK_METHOD` (`tests/rhi/MockDevice.h`) and a `NullDevice` override in the same change. Work test-first, and run the GPU tests on lavapipe with validation and sync validation enabled.

## Recommended order
1. RND-001, RND-004, RND-008: correctness and robustness of the frame loop (crash and UB paths). These are small and independent.
2. RND-002, RND-003, RND-014: the dynamic-geometry path. This is the biggest risk for the destruction use case. RND-002 (asynchronous uploads) comes before RND-003 (sub-allocating pool) because the pool's free path needs timeline-retired ranges.
3. RND-005, RND-016, RND-017: make capacity and frame-graph misuse visible instead of silent.
4. RND-007, RND-013: debuggability. Do these early enough to measure the work in step 2.
5. RND-006, RND-009, RND-010, RND-011, RND-012, RND-015, RND-018: performance, portability and polish.

## RND-001 — Device loss and other Vulkan failures abort in debug builds and are silently ignored in shipping builds
- **Severity / Effort:** High / L
- **Depends on:** None
- **Steps:**
  1. Add `ErrorCode::DeviceLost` (core `Error.h`) and map `VK_ERROR_DEVICE_LOST` (and `VK_ERROR_SURFACE_LOST_KHR` → a surface-lost code or `Unsupported`) in `VkError` (`VulkanDevice.cpp:349`).
  2. Split `INTERI_VK_CHECK` into two kinds. Calls that really cannot fail in a correct program keep the assert. Calls that can fail at run time (`vkQueueSubmit2`, `vkWaitSemaphores`, `vkGetSemaphoreCounterValue`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR`, `vkAllocateCommandBuffers`, `vkBeginCommandBuffer`/`vkEndCommandBuffer`) check the result in every build. On `DEVICE_LOST` they set an `Impl::deviceLost` flag.
  3. Expose the state on `IDevice`: `[[nodiscard]] virtual bool IsLost() const = 0;`, or have `Submit` return `Result<std::uint64_t>`. Once lost, `WaitForValue` returns immediately and `CompletedValue` returns `LastSubmittedValue`, so deferred destruction and destructors can still run.
  4. On loss, log the device name, the last submitted and completed timeline values, and the frame-graph pass list of the last frame. Enable `VK_EXT_device_fault` when available and log its vendor info.
  5. In `EditorRenderer::Render`, tell "minimised" (`InvalidArgument`) apart from a lost device or surface. On loss, autosave the document (the `AutosaveTimer` path exists), report it to the user, and recreate the device or exit cleanly.
  6. Use a finite timeout for `vkWaitSemaphores` (for example 5 s) and treat a timeout as a suspected hang (log it, then keep waiting or declare the device lost).
- **Illustrative change (not applied):**
  ```cpp
  // VulkanDevice.cpp
  const VkResult r = state.vk.vkQueueSubmit2(state.queue, 1, &submit, VK_NULL_HANDLE);
  if (r == VK_ERROR_DEVICE_LOST) { state.MarkLost("vkQueueSubmit2"); return value; }
  INTERI_ASSERT(r == VK_SUCCESS, "vkQueueSubmit2");
  ```
- **Verification:** A unit test with `MockDevice` that reports loss checks that `ResourceManager`/`CommandListPool` destructors don't hang. A manual test with a deliberately hanging compute shader under `--validation 1` checks that the editor logs a device-lost report and doesn't spin. Grep check: no `INTERI_VK_CHECK` remains on a submit, wait, acquire or present call.
- **Risks / trade-offs:** Recreating the device is a large job (every `IDevice` user must rebuild its resources). An acceptable first step is "detect, report, save, exit".

## RND-002 — Every mesh or render-feature upload creates a staging buffer and command pool, then blocks until the GPU is idle
- **Severity / Effort:** High / L
- **Depends on:** RND-001 (Submit result semantics), coordination with the Destruction Module owner
- **Steps:**
  1. Give `EditorRenderer::State` one long-lived `rhi::UploadQueue` (it is already a chunked ring that reuses chunks retired by the timeline) instead of building one in each `UploadNow`.
  2. Record uploads into the frame's own command list. Start the frame's command list (`commandPools->Acquire()`) before features gather, pass it to `AddMesh` and to `RenderFeatureContext::upload`, and call `uploads.Submitted(value)` after the frame's `Submit`. The barriers already in `MeshPool::Add` order the copies before the draws in the same submission.
  3. Change the `RenderFeatureContext::upload` contract (`EditorExtensionsApi.h:209`) from "submits it and waits" to "records into this frame's upload list; ranges are drawable in this frame". This changes the module ABI, so bump `INTERI_MODULE_ABI_VERSION` and update the Destruction Module.
  4. Keep a synchronous path only for one-off loads at startup, and log when it is used during play.
  5. Batch mesh uploads per frame: one barrier pair for all meshes added in a frame, not per mesh (see RND-003).
- **Illustrative change (not applied):**
  ```cpp
  // EditorRenderer::Render
  rhi::ICommandList& commands = state.commandPools->Acquire();   // before GatherRenderObjects
  context.upload = [&](const UploadFunction& record) { return record(*state.uploads, commands); };
  ...
  const std::uint64_t value = state.device->Submit(lists, *state.swapchain);
  state.uploads->Submitted(value);
  ```
- **Verification:** Add a counter or Tracy zone around `WaitForValue`. In a scene where a module adds N meshes per frame, the count of `WaitForValue` calls per frame is 0 (only `CommandListPool::BeginFrame`'s frame-in-flight wait remains). Run a GPU test that adds meshes and draws them in the same frame under sync validation with zero errors.
- **Risks / trade-offs:** Uploads now share the frame's command buffer, so a very large upload delays that frame. Cap bytes per frame and spill to the next frame if needed. The module ABI change needs coordinated releases.

## RND-003 — The append-only MeshPool is cleared with a full GPU drain when it fills, and the clear drops or thrashes the editor's own meshes
- **Severity / Effort:** High / L
- **Depends on:** RND-002
- **Steps:**
  1. Replace the bump pointer in `MeshPool` with a range allocator (for example a TLSF-like free list over vertex and index ranges; `TlsfAllocator` in core is a model). Add `MeshPool::Free(const MeshRange&)`. Freeing goes through a retire list keyed by `device.LastSubmittedValue() + 1`, like `ResourceManager`, so a range is reused only after the GPU finished the frames that drew it.
  2. Give the editor's `sceneMeshes` a reference count or LRU, and free ranges when meshes leave the document. Give render features a way to free their ranges (destroyed debris).
  3. Keep the "pool full" fallback as a last resort only. If it triggers, re-add the editor's meshes after the feature retry, in the same frame (`EditorRenderer.cpp:311-317`), so nothing disappears for a frame. Log a warning with the pool's high-water mark.
  4. Optionally make the pool growable: allocate a larger buffer, copy on the GPU, and retire the old one through `ResourceManager`. This removes the hard 1M vertex / 4M index cap.
  5. Batch barriers. Issue one `ShaderRead/IndexBuffer → CopyDst` and one back per frame for all pending adds, not two pairs per mesh.
- **Illustrative change (not applied):**
  ```cpp
  class MeshPool {
  public:
      Result<MeshRange> Add(rhi::UploadQueue&, rhi::ICommandList&, const MeshData&);
      void Free(const MeshRange& range, std::uint64_t retireValue); // reused once CompletedValue() >= retireValue
      void Collect(std::uint64_t completedValue);
  };
  ```
- **Verification:** Unit tests (MockDevice): Add/Free/Add reuses space only after the completed value passes the retire value, and fragmentation with mixed sizes stays bounded. A GPU golden test adds and frees debris meshes over 1000 frames with constant pool usage and zero validation errors. In the editor, a scene that previously triggered "mesh pool full" shows no flicker and no `ResetMeshPool` call.
- **Risks / trade-offs:** Free-list fragmentation for very uneven chunk sizes. Consider size classes for debris. `MeshRange` users must not hold ranges past `Free`.

## RND-004 — An acquired swapchain image is never presented on the editor's early-return paths
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `EditorRenderer::Render`, move the `frame.ui == nullptr` check before `AcquireNextImage`.
  2. After a successful acquire, always submit and present. On `AddPass`/`Compile`/`Execute` failure, record an empty frame (clear the image, transition it to `Present`) instead of returning early.
  3. Alternatively, add `ISwapchain::Abandon()` that waits on the acquire semaphore through an empty submission, so the slot's semaphore is consumed and `m_acquired` is reset.
- **Verification:** A headless GPU test (`tests/rhi_vulkan/SwapchainTests.cpp` style, or an `EditorRenderer` test with an injected failing pass) runs three frames in a row where the frame graph fails to compile. There is no assert and no validation error, and frame four presents normally.
- **Risks / trade-offs:** None significant.

## RND-005 — Forward-renderer capacity limits are checked only by asserts, and the scene-buffer ring trusts the caller's frame pacing
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. In `ForwardRenderer::AddPasses`, clamp `objects` and `pointLights` to capacity in every build and log a warning once per change (as `ImGuiRenderer::Prepare` already does). Or return `Result<ForwardOutputs>`.
  2. Record the timeline value per slot. `AddPasses` takes the value the frame will be submitted with (or `device.LastSubmittedValue() + 1`) and checks with `INTERI_VERIFY` (or waits) that the slot's previous value is complete before writing. Apply the same to `DecalRenderer` and `GpuParticleSystem`.
  3. Document, or enforce with an assert, "one `AddPasses` per frame per renderer instance". Offer a second instance or more slots for multi-view.
  4. In the editor, surface the truncation (`EditorRenderer.cpp:334-337`) in a stats overlay (see RND-007).
- **Verification:** A unit test with `NullDevice`/`MockDevice`: 4097 objects with `maxObjects = 4096` doesn't write past the mapped span (check with an ASan-instrumented fake mapped buffer). A test calling `AddPasses` twice before the GPU completes asserts or waits instead of overwriting.
- **Risks / trade-offs:** The clamp hides content. Make the warning visible.

## RND-006 — Pipelines are created without a VkPipelineCache, and nothing is persisted between runs
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Create one `VkPipelineCache` in `VulkanDevice::Create`, optionally seeded from a blob in `VulkanDeviceDesc` (validate the header: vendor, device, UUID). Pass it to both `vkCreate*Pipelines` calls.
  2. Add `VulkanDevice::PipelineCacheData()` (or an `IDevice` virtual) and save it from the editor and game on shutdown to the user cache folder.
  3. Later: compile pipelines on a job worker (creation is already documented as callable from any thread) and add a pipeline library or warm-up list for module features.
- **Verification:** A test creates the same pipelines twice through a seeded cache with the device created on the second run, and the blob is non-empty and accepted. Measure first-frame time of `level_editor --frames 1` before and after.
- **Risks / trade-offs:** Stale or corrupt blobs. Rely on the driver's header check and delete the file on mismatch.

## RND-007 — No GPU debug markers, no GPU timestamps and no render statistics
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Add `ICommandList::BeginMarker(std::string_view)` / `EndMarker()` (Vulkan: `vkCmdBeginDebugUtilsLabelEXT` when `debugUtils`, otherwise no-op). Add mocks and `NullDevice` overrides.
  2. Wrap each frame-graph pass in `FrameGraph::Execute` with `pass.name` markers, and the Hi-Z mips and shadow cascades inside their passes.
  3. Add a timestamp query pool per frame in flight (`ICommandList::WriteTimestamp(slot)` + `IDevice::ReadTimestamps(frame)`), recorded around each pass, and expose per-pass GPU milliseconds. Optionally feed Tracy GPU zones in `*-profile` presets.
  4. Read back `drawCounts` (4 bytes per view) one frame late from a small readback buffer for a stats overlay: visible objects per view, triangles, `MeshPool` usage, VMA budget (RND-011).
  5. Add `INTERI_ZONE` CPU zones in `ForwardRenderer::AddPasses`, `FrameGraph::Compile/Execute` and `EditorRenderer::Render`.
- **Verification:** A RenderDoc capture of `level_editor` shows named pass regions. A unit test with `MockDevice` expects `BeginMarker`/`EndMarker` pairs per non-culled pass. The editor overlay shows non-zero GPU times on lavapipe.
- **Risks / trade-offs:** Timestamp queries need `timestampValidBits` on the queue family, so check it and disable timing when it is zero.

## RND-008 — CreateTexture destroys the texture immediately on failure while the caller's command list still references it
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Check every mip's size against the upload chunk before recording anything (`UploadQueue` can expose `ChunkSize()`), and return early before the first barrier.
  2. If a failure can still happen after recording, retire the texture through `ResourceManager::DestroyTexture` (add a `ResourceManager&` parameter) instead of `IDevice::DestroyTexture`, and document that the command list must still be submitted or discarded.
- **Illustrative change (not applied):**
  ```cpp
  for (const ImageRgba8& mip : mips)
      if (RowPitchSize(mip) > uploads.ChunkSize()) return MakeError(ErrorCode::InvalidArgument, "mip larger than an upload chunk");
  // ... only now CreateTexture + barriers + uploads
  ```
- **Verification:** A unit test with `MockDevice`: an oversized mip records no barrier, makes no `CreateTexture` call and returns `InvalidArgument`.
- **Risks / trade-offs:** None.

## RND-009 — The swapchain acquire wait blocks all GPU work in the frame at ALL_COMMANDS
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Wait on the acquire semaphore at `VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT` (plus `COPY` if the image is a copy destination).
  2. Make the swapchain image's first barrier use `srcStage = COLOR_ATTACHMENT_OUTPUT` (not `NONE`) so the layout transition is ordered after the semaphore wait. For example, map `ResourceState::Undefined` on swapchain-owned textures to that source stage, or have `FrameGraph` import the swapchain with a dedicated `Acquired` state.
- **Verification:** The GPU tests and the headless swapchain tests stay clean under sync validation. A RenderDoc or Nsight timeline shows culling and shadows starting before the acquire completes.
- **Risks / trade-offs:** Getting the source stage wrong is a classic WSI hazard. Keep sync validation on in the swapchain tests.

## RND-010 — The command-recording hot path takes a shared mutex and copies heap-allocated TextureData for every handle lookup
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Return a small POD view from `Impl::Texture()` (image, view, aspect, format, extent, mip and layer counts, bindless), and keep the per-mip and per-layer vectors behind index lookups such as `LayerView(handle, i)` and `StorageSlot(handle, mip)`.
  2. Replace the `std::vector` scratch in `Barriers`/`BeginRendering` with `SmallVector` or `FixedVector` (core containers).
  3. Consider a lock-free read path (an epoch or a generation-checked `SlotMap` read). Creation and destruction already take the unique lock.
- **Verification:** A benchmark (`benchmarks/`, NullDevice-style plus the Vulkan device in a GPU test) of recording 10k barriers and attachments shows zero allocations per call (count with a tracking allocator) and reduced time.
- **Risks / trade-offs:** Lock-free reads need care under TSan. Keep the shared lock if contention isn't measured.

## RND-011 — No GPU memory budget tracking, and the UploadQueue staging ring only grows
- **Severity / Effort:** Low / S
- **Depends on:** RND-007 (overlay)
- **Steps:**
  1. Enable `VK_EXT_memory_budget` when it is present, set `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`, and expose `IDevice::MemoryBudget()` (usage and budget per heap).
  2. In `UploadQueue`, free chunks that have been retired and idle for N frames once more than a configurable number are cached.
- **Verification:** A unit test (`tests/rhi/UploadQueueTests.cpp`): after a burst that creates 8 chunks and then 100 idle frames, at most the configured count remain. The editor overlay shows the budget.
- **Risks / trade-offs:** Chunk churn if the threshold is too low.

## RND-012 — Device and swapchain creation are Vulkan-specific in the editor, with no backend factory
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add a backend-neutral factory, `rhi::CreateDevice(const DeviceDesc&)`, that selects the compiled-in backend, plus `IDevice::CreateSwapchain(NativeSurface, const SwapchainDesc&)` or a separate `ISwapchainFactory`.
  2. Make `EditorRenderer::State::device` a `std::unique_ptr<rhi::IDevice>`, and move `NativeInstance`/surface creation behind a platform-provided `NativeWindowSurface` callback.
- **Verification:** `tools/editor` compiles without including `interi/rhi_vulkan/VulkanDevice.h` except in one backend-selection TU. The console backends (private repos) build the editor without patches.
- **Risks / trade-offs:** Adds a virtual to `IDevice` (mock and NullDevice updates).

## RND-013 — The editor runs without validation layers by default, even in debug builds
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Default `validation` to `true` when `INTERI_ENABLE_ASSERTS` is on and not shipping, keeping `--validation 0` to opt out.
  2. Surface `VulkanDevice::ValidationErrorCount()` in the console panel or overlay, and in the `--frames N` smoke run's exit code.
- **Verification:** `level_editor --video-driver offscreen --frames 120` in the debug preset reports validation enabled when the layer is installed, and the CI smoke run fails on any validation error.
- **Risks / trade-offs:** Slower debug editor. The opt-out flag remains.

## RND-014 — The editor de-indexes every scene mesh, tripling vertex memory in the shared pool
- **Severity / Effort:** Low / S
- **Depends on:** None (complements RND-003)
- **Steps:**
  1. In `EditorRenderer::AddMesh`, weld per slot on (position, corner normal) with a hash map, emitting shared vertices and real indices. Or run `meshopt_generateVertexRemap` (meshoptimizer is already a dependency of `asset_import`; if you add it to the editor, keep it licence-checked).
- **Verification:** A unit test on a generated sphere primitive: vertex count drops to the number of unique (position, normal) pairs and the rendered golden image is unchanged.
- **Risks / trade-offs:** CPU cost at add time. It is paid once per mesh.

## RND-015 — Swapchain recreation reuses present semaphores without a guarantee that pending presents have finished
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. When `VK_EXT_swapchain_maintenance1` is available, attach a present fence (`VkSwapchainPresentFenceInfoEXT`) and only destroy, reuse or retire semaphores and old swapchains after their fences signal. Otherwise, keep old swapchains and their semaphores in a retire list for a few frames.
  2. Drop the `vkQueueWaitIdle` in `Recreate` once retirement is tracked.
  3. Later: an HDR colour-space choice (`VK_COLOR_SPACE_HDR10_ST2084_EXT`) behind a setting.
- **Verification:** The swapchain resize test (`Swapchain_Resize_Recreates`) is extended to 100 resizes with presents in between and is clean under validation, including the WSI semaphore checks.
- **Risks / trade-offs:** Extension availability varies. Keep the conservative fallback.

## RND-016 — FrameGraph accesses carry one ResourceState, and reading a resource in two states in one pass produces unordered barriers
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `FrameGraph::Compile`, reject (return `InvalidArgument`) a pass that accesses the same resource more than once with different states, naming the pass and resource.
  2. Longer term: let an access carry a combined read mask (for example `IndirectArgument | ShaderRead`) and translate it to the union of stages and accesses in the backend.
- **Verification:** A `FrameGraphTests.cpp` case: a pass reading a buffer as `IndirectArgument` and `ShaderRead` fails `Compile` with a clear message.
- **Risks / trade-offs:** None.

## RND-017 — Running out of bindless storage-image slots aborts instead of returning an error, and view-creation failures are unchecked in shipping
- **Severity / Effort:** Low / S
- **Depends on:** RND-001
- **Steps:**
  1. In `RegisterTexture`, allocate storage-image slots before creating views, and return `OutOfMemory` (releasing already-allocated slots and views) like the other tables.
  2. Check the `vkCreateImageView` results for layer and mip views and return `VkError` on failure.
- **Verification:** A unit test with a tiny table (inject capacity): the second storage texture returns `OutOfMemory`, and the device destructor reports no leaks.
- **Risks / trade-offs:** None.

## RND-018 — Particles issue two full-capacity draws each frame, whatever the live count
- **Severity / Effort:** Low / M
- **Depends on:** HLSL analyst (shader side)
- **Steps:**
  1. Have the simulate pass append live particle indices per blend mode to an alive list with an atomic counter, and write indirect draw arguments (`DrawIndirect` would need a new RHI entry point, or reuse `DrawIndexedIndirectCount` with a quad index buffer).
  2. Draw each blend mode with its live count only.
- **Verification:** The existing particle GPU and golden tests stay unchanged. GPU timing (RND-007) for the particle pass scales with live particles, not capacity.
- **Risks / trade-offs:** Order changes inside a blend mode could change goldens. Sort or keep ring order.
