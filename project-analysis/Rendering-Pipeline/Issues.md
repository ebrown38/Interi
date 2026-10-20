# Rendering-Pipeline — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi) @ 4631f35
**Analyzed:** 2026-10-19
**Scope:** CPU-side rendering and graphics-API usage: `engine/rhi` (IDevice/ICommandList/ISwapchain, ResourceManager, CommandListPool, UploadQueue), `engine/rhi_vulkan/src` (VulkanDevice.cpp 1796 lines, VulkanSwapchain.cpp, VulkanImpl.h), `engine/render/src` (FrameGraph, ForwardRenderer, MeshPool, Particles, Decals, Texture), `engine/editor_ui/src/ImGuiRenderer.cpp`, `EditorExtensions.cpp` (render features), `tools/editor/EditorRenderer.cpp` and the render call site in `EditorApp.cpp`. I also checked which tests exist under `tests/rhi`, `tests/rhi_vulkan` and `tests/render`. HLSL source is out of scope (HLSL analyst). `modules/` (Destruction Module) is gitignored and not indexed, so I only reviewed it through the engine API it uses (`RenderFeatureContext`).
**Summary:** 20 issues — Critical: 0 · High: 3 · Medium: 5 · Low: 10 · Info: 2

## Findings

### RND-001 — Device loss and other Vulkan failures abort in debug builds and are silently ignored in shipping builds
- **Severity:** High
- **Confidence:** High
- **Category:** API correctness / Device loss
- **Location:** `engine/rhi_vulkan/src/VulkanImpl.h:23-25`, `engine/rhi_vulkan/src/VulkanDevice.cpp:359-362`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1732`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1773`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1783`, `engine/core/include/interi/core/Assert.h:107-111`, `engine/rhi_vulkan/src/VulkanSwapchain.cpp:246-250`, `tools/editor/EditorRenderer.cpp:261-265`
- **Evidence:** All frame-critical calls go through `INTERI_VK_CHECK`, which is `INTERI_VERIFY(result == VK_SUCCESS, call)`. That covers `vkQueueSubmit2` (line 1732), `vkGetSemaphoreCounterValue` (1773), `vkWaitSemaphores(..., UINT64_MAX)` (1783), `vkAllocateCommandBuffers` (1672) and `vkBeginCommandBuffer` (1678). With asserts on, `VK_ERROR_DEVICE_LOST` goes to the assert handler and aborts. With `INTERI_SHIPPING`, `INTERI_VERIFY` becomes `(void)(condition)` (Assert.h:107-111), so the error is dropped, the timeline value is treated as signaled, and recording continues on a lost device (or on a `VK_NULL_HANDLE` command buffer if allocation failed). `IDevice` has no "device lost" state or query, and `ErrorCode` has no device-lost code: `VkError` maps every other `VkResult` to `ErrorCode::Unknown` (VulkanDevice.cpp:349-355). When `vkAcquireNextImageKHR` returns `VK_ERROR_DEVICE_LOST` or `VK_ERROR_SURFACE_LOST_KHR`, the result is `ErrorCode::Unknown` (VulkanSwapchain.cpp:246-250). The editor treats any acquire failure as "minimised: nothing to draw into" (EditorRenderer.cpp:261-265), so it keeps looping without logging anything. Nothing dumps diagnostics (no `VK_EXT_device_fault`, no breadcrumbs).
- **Impact:** A GPU hang, TDR or driver reset (common on Windows during heavy destruction or shader iteration) either aborts the editor without a useful report or leaves it spinning with no output. The editor cannot recreate the device, so users lose unsaved work and no data is collected to debug the cause.

### RND-002 — Every mesh or render-feature upload creates a staging buffer and command pool, then blocks until the GPU is idle
- **Severity:** High
- **Confidence:** High
- **Category:** Dynamic geometry / Upload path
- **Location:** `tools/editor/EditorRenderer.cpp:37-54`, `tools/editor/EditorRenderer.cpp:166-213`, `tools/editor/EditorRenderer.cpp:291-292`, `engine/editor_ui/include/interi/editor_ui/EditorExtensionsApi.h:209-215`, `tools/editor/EditorApp.cpp:957`
- **Evidence:** `UploadNow` creates a new `rhi::UploadQueue(device, 4 MiB)` (a new host-visible VMA buffer), a new command pool and a command list. It then submits and calls `device.WaitForValue(value)` before destroying them. The device has one queue, so waiting for this submission also waits for every earlier frame still in flight, which drains the GPU completely. `EditorRenderer::AddMesh` uses it once for each new mesh. `EditorApp::BuildScene` calls `AddMesh` for every object during frame building (EditorApp.cpp:957). The render-feature API hands the same function to modules: "Records mesh and texture uploads into a one-off command list, submits it and waits" (EditorExtensionsApi.h:209). That is the path the Destruction Module uses for fractured chunks and debris.
- **Impact:** Each break event, imported model or new debris mesh costs a 4 MiB allocation and free, a pool creation and a full CPU/GPU synchronization in the middle of the frame. The skill rates per-break GPU allocations and stalls as High: a fracture that spawns many chunks stalls the frame once per upload call. This is the opposite of the pooled, asynchronous dynamic-geometry path a destruction engine needs.

### RND-003 — The append-only MeshPool is cleared with a full GPU drain when it fills, and the clear drops or thrashes the editor's own meshes
- **Severity:** High
- **Confidence:** High
- **Category:** GPU memory / Dynamic geometry
- **Location:** `engine/render/include/interi/render/MeshPool.h:33-57`, `engine/render/src/MeshPool.cpp:64-95`, `tools/editor/EditorRenderer.cpp:150-164`, `tools/editor/EditorRenderer.cpp:201-207`, `tools/editor/EditorRenderer.cpp:280-301`, `tools/editor/EditorRenderer.cpp:311-317`, `engine/editor_ui/src/EditorExtensions.cpp:406-440`
- **Evidence:** `MeshPool` is "appended and never freed". `Clear()` only resets the counters, and the caller has to wait for the GPU first. `ResetMeshPool` calls `WaitForValue(LastSubmittedValue())`, empties `sceneMeshes` and re-uploads the cube. `Render` gathers render features first. If a feature returns `OutOfMemory`, `GatherRenderObjects` calls `resetPool()` and retries, and that empties `state.sceneMeshes`. The editor's own meshes, added earlier in the frame by `BuildScene`, then fail the lookup at line 313 and are skipped (`continue`) for that frame, contrary to the comment at 280-281. On the next frame `BuildScene` re-adds them (one `UploadNow` stall per mesh, RND-002). If editor meshes plus feature meshes together exceed the pool (1M vertices / 4M indices, line 126), every frame repeats the cycle: editor adds → feature OOM → reset → editor meshes missing. `MeshPool::Add` also issues two whole-buffer barriers per mesh (MeshPool.cpp:79-89).
- **Impact:** Long destruction sessions (debris accumulating) or large levels cause hitches (a full drain plus re-upload of everything), visible one-frame flicker, and a possible stall on every frame. There is no free list or sub-allocator, so destroyed chunks never return memory. Graph: `MeshPool` is the shared geometry store for `ForwardRenderer::m_meshes`, `RenderFeatureContext.meshes`, the editor state and the GPU tests (`context MeshPool`). Every GPU-driven draw depends on it.

### RND-004 — An acquired swapchain image is never presented on the editor's early-return paths
- **Severity:** Medium
- **Confidence:** High
- **Category:** Synchronization / Swapchain
- **Location:** `tools/editor/EditorRenderer.cpp:259-271`, `tools/editor/EditorRenderer.cpp:364-376`, `engine/rhi_vulkan/src/VulkanSwapchain.cpp:227`, `engine/rhi_vulkan/src/VulkanSwapchain.cpp:260-265`
- **Evidence:** `Render` calls `AcquireNextImage()`. It then returns `false` without submitting or presenting when `frame.ui == nullptr` (268-271), when `ui->AddPass` fails (364-370, e.g. a failed ImGui atlas upload) or when `graph.Compile()` fails (372-376). `m_acquired` stays true, so the next `AcquireNextImage` hits `INTERI_ASSERT(!m_acquired, ...)` and aborts in debug builds. In builds without asserts, the acquire semaphore of that slot is left signaled with no waiter and its `lastUse` is never updated (BeginSubmit was skipped). The slot is reused `images + 1` acquires later, and acquiring with an already signaled semaphore is a Vulkan validity error that can deadlock or fail.
- **Impact:** One transient failure (texture upload OOM, a frame-graph authoring error in a module pass) turns into a crash or a swapchain stuck in an invalid state. Today `EditorApp` always passes `ImGui::GetDrawData()`, so only the error paths are reachable.

### RND-005 — Forward-renderer capacity limits are checked only by asserts, and the scene-buffer ring trusts the caller's frame pacing
- **Severity:** Medium
- **Confidence:** High
- **Category:** Frames in flight / Memory safety
- **Location:** `engine/render/src/ForwardRenderer.cpp:240-244`, `engine/render/src/ForwardRenderer.cpp:281-306`, `engine/render/include/interi/render/ForwardRenderer.h:100-102`, `tools/editor/EditorRenderer.cpp:334-337`
- **Evidence:** `AddPasses` checks `objects.size() <= maxObjects` and `pointLights.size() <= kMaxPointLights` only with `INTERI_ASSERT`, then `memcpy`s every object and light into the persistently mapped slot. When asserts are compiled out (shipping), an oversized frame writes past the end of the mapped upload buffer. The slot is chosen by `m_nextSlot` round-robin with no timeline check. Correctness depends on a header comment ("must not be called for frame N + framesInFlight before frame N finished") and on the caller's `CommandListPool` having the same `framesInFlight`. `DecalRenderer` and `GpuParticleSystem` use the same pattern (Decals.cpp:123-124, Particles.cpp:148-149). The editor truncates objects silently (334-337) and never passes lights.
- **Impact:** A game or module that renders more than 4096 objects (the default `maxObjects`; debris counts can exceed it) corrupts memory in shipping builds. A second `AddPasses` call in one frame (a second view, a preview or a split screen) overwrites data the GPU is still reading. Objects over the cap disappear without a warning.

### RND-006 — Pipelines are created without a VkPipelineCache, and nothing is persisted between runs
- **Severity:** Medium
- **Confidence:** High
- **Category:** PSO creation and caching
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:1441-1442`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1573-1574`
- **Evidence:** `vkCreateComputePipelines(state.device, VK_NULL_HANDLE, ...)` and `vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, ...)` pass a null cache. A text search for `VkPipelineCache` or `pipelineCache` finds nothing in `engine/`, `tools/` or `tests/`. Pipelines are created synchronously when renderers are constructed, and no precompile or async step exists.
- **Impact:** Every launch pays full driver compilation. This is cheap with today's roughly 10 pipelines, but it grows with each material, debris, decal and VFX permutation and with each module render feature, and it causes first-use hitches on Windows drivers. PSO caching is also a certification expectation on the console backends the RHI is designed for.

### RND-007 — No GPU debug markers, no GPU timestamps and no render statistics
- **Severity:** Medium
- **Confidence:** High
- **Category:** Debuggability
- **Location:** `engine/rhi/include/interi/rhi/Device.h:19-77`, `engine/render/src/FrameGraph.cpp:490-510`, `engine/rhi_vulkan/src/VulkanDevice.cpp:390-402`
- **Evidence:** `ICommandList` has no begin/end label or timestamp method. `FrameGraph::Execute` knows every `pass.name` but never emits it. Text searches for `DebugUtilsLabel`, `vkCmdWriteTimestamp`, `QueryPool`, `TracyVk` and `INTERI_ZONE` in `engine/rhi_vulkan`, `engine/render` and `EditorRenderer.cpp` find nothing. Only object names are set (`SetName`, called with `debugName`). Nothing reports draw counts (the GPU-side `drawCounts` are never read back), triangle counts, memory use or per-pass timings.
- **Impact:** RenderDoc, Nsight and PIX captures show one unlabelled command buffer, and there is no way to measure the cost of the Hi-Z, cull, shadow or forward passes, or of destruction spikes. This blocks evidence-based tuning of the GPU-driven pipeline.

### RND-008 — CreateTexture destroys the texture immediately on failure while the caller's command list still references it
- **Severity:** Medium
- **Confidence:** High
- **Category:** Resource lifetime
- **Location:** `engine/render/src/Texture.cpp:55-65`
- **Evidence:** A `Undefined→CopyDst` barrier and the copies for earlier mips are recorded into the caller's `commands`. If a later `UploadTexture` fails (for example, a mip larger than one upload chunk, UploadQueue.cpp:78-81), the function calls `device.DestroyTexture(texture)` immediately, not `ResourceManager::DestroyTexture`. The command list has not been submitted and still names the destroyed `VkImage`.
- **Impact:** If the caller shares the command list with other work and submits it (the natural thing for a per-frame list), the submission uses a destroyed image. That is undefined behaviour and can lose the device. `ImGuiRenderer::UpdateTexture` happens not to submit on failure, but the function is a public engine API (`Texture.h`).

### RND-009 — The swapchain acquire wait blocks all GPU work in the frame at ALL_COMMANDS
- **Severity:** Low
- **Confidence:** High
- **Category:** Synchronization / Throughput
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:1713-1724`, `engine/rhi_vulkan/src/VulkanDevice.cpp:310-311`
- **Evidence:** `SubmitImpl` waits on the acquire semaphore with `stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT`. The editor records the whole frame (object cull, Hi-Z, light culling, shadows, forward, decals, UI) in one submission, so none of it starts until the presentation engine releases the image. The broad mask is needed today because the `Undefined` state maps to `srcStage = NONE`, so a narrower wait would leave the swapchain layout transition unordered.
- **Impact:** Little overlap between presentation and the next frame's off-screen work. The cost is small at editor frame rates, but it is a latency and throughput limit for a GPU-driven runtime.

### RND-010 — The command-recording hot path takes a shared mutex and copies heap-allocated TextureData for every handle lookup
- **Severity:** Low
- **Confidence:** High
- **Category:** RHI design / CPU cost
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:366-388`, `engine/rhi_vulkan/src/VulkanImpl.h:94-113`, `engine/rhi_vulkan/src/VulkanDevice.cpp:436`, `engine/rhi_vulkan/src/VulkanDevice.cpp:538`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1341`
- **Evidence:** `Impl::Texture()` takes `std::shared_lock` and returns `TextureData` by value. `TextureData` holds three `std::vector`s (`layerViews`, `mipViews`, `storageBindless`), so each texture barrier, `BeginRendering` attachment, copy and `BindlessStorageIndex` call allocates. The Hi-Z pass alone calls `BindlessStorageIndex` twice per mip (ForwardRenderer.cpp:429-431). `Barriers` and `BeginRendering` also build a `std::vector` per call (VulkanDevice.cpp:415-432, 531).
- **Impact:** Avoidable CPU cost and lock contention once recording is spread across job workers (CommandListPool already supports this). It will show up as pass and draw counts grow.

### RND-011 — No GPU memory budget tracking, and the UploadQueue staging ring only grows
- **Severity:** Low
- **Confidence:** High
- **Category:** GPU memory
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:893-898`, `engine/rhi/src/UploadQueue.cpp:113-137`
- **Evidence:** VMA is created without `VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT`, and no code calls `vmaGetHeapBudgets`. `UploadQueue::NextChunk` reuses retired chunks but appends a new chunk whenever none has retired, and chunks are freed only in the destructor.
- **Impact:** Out-of-memory conditions surface only as allocation failures, and there is no data for streaming or debris budgets. A burst of uploads (a level load, a large fracture) permanently raises staging memory.

### RND-012 — Device and swapchain creation are Vulkan-specific in the editor, with no backend factory
- **Severity:** Low
- **Confidence:** High
- **Category:** RHI architecture
- **Location:** `engine/rhi_vulkan/include/interi/rhi_vulkan/VulkanDevice.h:80-89`, `tools/editor/EditorRenderer.cpp:16`, `tools/editor/EditorRenderer.cpp:62`, `tools/editor/EditorRenderer.cpp:112-121`
- **Evidence:** `ISwapchain` is backend-neutral, but you can only create one through `VulkanDevice::CreateSwapchain(std::uint64_t surface, ...)`, and `IDevice` has no swapchain factory. `EditorRenderer::State` holds `std::unique_ptr<rhi::VulkanDevice>` and calls `VulkanDevice::Create`, `NativeInstance()` and `DeviceName()` directly. Graph check: no `engine/` file outside `rhi_vulkan`/`platform_sdl` imports Vulkan, VMA or volk headers (the cypher query returned 0 rows), so the leak is limited to this construction seam.
- **Impact:** The private PS5/Xbox backends, or a future D3D12 backend, have to patch the editor (and any future game player) instead of selecting a backend. The rest of the renderer is cleanly backend-neutral.

### RND-013 — The editor runs without validation layers by default, even in debug builds
- **Severity:** Low
- **Confidence:** High
- **Category:** Validation
- **Location:** `tools/editor/main.cpp:43`, `tools/editor/main.cpp:77-79`
- **Evidence:** `bool validation = false;` is enabled only by `--validation 1`. GPU tests enable validation and synchronization validation and fail on any error (VulkanDevice.cpp:804-814; CLAUDE.md), but the editor, which is the only interactive renderer and the host of module render features, does not.
- **Impact:** Hazards introduced by module render features, resize paths or the editor's extra passes are not caught during daily development.

### RND-014 — The editor de-indexes every scene mesh, tripling vertex memory in the shared pool
- **Severity:** Low
- **Confidence:** High
- **Category:** GPU memory / Geometry
- **Location:** `tools/editor/EditorRenderer.cpp:173-184`
- **Evidence:** "One render mesh per slot: three vertices per triangle". Each corner becomes a new vertex with index `vertices.size()`, so the welded positions from `scene::TriangleMesh` are discarded (the format has per-corner normals).
- **Impact:** Vertex memory is about 3x what an indexed mesh needs (for a closed mesh), and the post-transform vertex cache gets no reuse. Because the pool is append-only (RND-003), it fills about 3x sooner and the clear stall comes earlier.

### RND-015 — Swapchain recreation reuses present semaphores without a guarantee that pending presents have finished
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Swapchain
- **Location:** `engine/rhi_vulkan/src/VulkanSwapchain.cpp:99-100`, `engine/rhi_vulkan/src/VulkanSwapchain.cpp:179-183`, `engine/rhi_vulkan/src/VulkanSwapchain.cpp:214-217`
- **Evidence:** `Recreate` calls `vkQueueWaitIdle` and then destroys the old swapchain right after creating the new one, keeping the per-image `m_renderedSemaphores`. Core Vulkan provides no fence for `vkQueuePresentKHR`, so a semaphore waited on by a pending present may not be safe to reuse. `VK_EXT_swapchain_maintenance1` (present fences) is not used. Present modes are FIFO, or MAILBOX/IMMEDIATE when `vsync` is off. HDR colour spaces are not considered.
- **Impact:** This is the well-known source of sporadic validation errors and rare driver issues during window resizes. The `vkQueueWaitIdle` also hitches every resize.

### RND-016 — FrameGraph accesses carry one ResourceState, and reading a resource in two states in one pass produces unordered barriers
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Frame graph / Barriers
- **Location:** `engine/render/src/FrameGraph.cpp:461-479`, `engine/render/src/FrameGraph.cpp:500-504`, `engine/rhi/include/interi/rhi/Types.h:117-132`
- **Evidence:** `transition` is called once per access. Two reads of the same resource in one pass (for example `IndirectArgument` and `ShaderRead`) push two barriers, `A→B` and `B→C`, into the same `vkCmdPipelineBarrier2` batch. The barriers in a batch are unordered, and the second barrier's source scope (a read state) does not make the earlier writes visible to `C`. `ResourceState` is an enum, not a combinable mask, and `Compile` does not reject this. No current pass does it: I checked every `AddPass` in ForwardRenderer, Decals, Particles and ImGuiRenderer.
- **Impact:** This is a latent hazard for new passes (a GPU-driven debris pass that reads the draw arguments both as indirect arguments and as a buffer is a natural candidate). Synchronization validation would catch it only in GPU tests.

### RND-017 — Running out of bindless storage-image slots aborts instead of returning an error, and view-creation failures are unchecked in shipping
- **Severity:** Low
- **Confidence:** High
- **Category:** Descriptor management
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:1069`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1083`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1090-1093`
- **Evidence:** `RegisterTexture` uses `INTERI_VERIFY(slot != kInvalidBindlessIndex, "bindless storage-image table is full")` while the buffer and sampled-texture tables return `OutOfMemory`. Per-layer and per-mip `vkCreateImageView` results go through `INTERI_VK_CHECK` (ignored in shipping, see RND-001). The Hi-Z pyramid uses one storage slot per mip (about 11 for 1080p), and its transient is re-created on each viewport resize.
- **Impact:** Behaviour is inconsistent: in shipping builds an exhausted table leaves an invalid slot in the descriptor, and a failed view creation leaves a null view.

### RND-018 — Particles issue two full-capacity draws each frame, whatever the live count
- **Severity:** Low
- **Confidence:** High
- **Category:** Throughput / VFX budget
- **Location:** `engine/render/src/Particles.cpp:227-234`
- **Evidence:** The alpha and the additive pipelines each `Draw(capacity * 6, 1, 0, 0)`, and every ring slot is processed (dead and wrong-blend particles are rejected in the shader). `ParticleSimulate` also dispatches over the full `capacity`. There is no compacted alive list and no indirect count.
- **Impact:** Vertex work scales with ring capacity rather than live particles, both twice per frame. Destruction dust and debris effects need large rings, so the GPU cost is paid even when the screen is quiet.

### RND-019 — Positive: occlusion and shadows are fully dynamic and survive destruction
- **Severity:** Info
- **Confidence:** High
- **Category:** Destruction-specific rendering
- **Location:** `engine/render/src/ForwardRenderer.cpp:347-440`, `engine/render/src/ForwardRenderer.cpp:486-525`
- **Evidence:** Hi-Z is rebuilt every frame from the current frame's `occluder` objects. Cascaded shadows are re-rendered every frame with GPU-culled indirect draws. There is no PVS, no baked portals, no lightmaps and no shadow cache to invalidate.
- **Impact:** Nothing to fix. Keep it this way: any future shadow caching or static occlusion must be invalidated by geometry changes from destruction.

### RND-020 — No UV path: all texturing is world-space triplanar
- **Severity:** Info
- **Confidence:** High
- **Category:** Destruction-specific rendering / Materials
- **Location:** `engine/render/include/interi/render/Mesh.h:13-18`, `engine/render/include/interi/render/ForwardRenderer.h:33-35`
- **Evidence:** `render::Vertex` is position plus normal (24 bytes). Imported UVs and tangents (`.imesh` v2) are dropped. This is documented in CLAUDE.md as "not here yet".
- **Impact:** Triplanar mapping suits fracture interior faces. Authored assets cannot use their UV-mapped or normal-mapped materials until a UV/tangent vertex stream is added. That is a planned feature, so this is a question of sequencing rather than a defect.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD (fresh) |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all third-party (SDL Joystick/Hidapi/Video, Tracy, Manifold, Vulkan headers); no first-party render cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL example/video flows from `external/vcpkg/buildtrees`; none first-party |
| 4 | query | "swapchain acquire present out of date resize" | `ISwapchain`, `VulkanSwapchain::AcquireNextImage` (225-258), `BeginSubmit`, `Present`; rest SDL_gpu noise |
| 5 | query | "frame graph barrier transient aliasing" | `FrameGraph::CreateTexture`, `Compile`, `CreateTransients`; rest pthread/wayland noise |
| 6 | query | "upload mesh pool vertex buffer" | `EditorRenderer::UploadNow`, `AddMesh`, `ResetMeshPool`, `MeshPool`, `MeshPool::Add` |
| 7 | context | `VulkanSwapchain::AcquireNextImage` (uid) | Calls `Recreate`, `MakeError`; no resolved incoming callers (virtual dispatch through `ISwapchain`) |
| 8 | context | `MeshPool` (class uid) | Used by `ForwardRenderer.m_meshes`, `RenderFeatureContext.meshes`, `EditorRenderer::State.meshes`, `RenderScene.h`, `ParticleGoldenTests` |
| 9 | impact | `ResetMeshPool` upstream | 0 callers, risk UNKNOWN (called through a lambda passed to `GatherRenderObjects`); confirmed by Read at EditorRenderer.cpp:295 |
| 10 | cypher | IMPORTS of vulkan/volk/vk_mem files from `engine/`, `tools/` outside `rhi_vulkan` and `platform_sdl` | 0 rows: no Vulkan header leak outside the backend |
| 11 | cypher | File list under `engine/rhi*` and `engine/render` | 60 files, which set the reading list (VulkanDevice.cpp, FrameGraph.cpp, ForwardRenderer.cpp, MeshPool.cpp ...) |
| 12 | cypher | CALLS into `AddPasses`/`AcquireNextImage`/`CreateSwapchain`/`WaitForValue`/`Clear` from non-test code | 4 unrelated rows (virtual calls are not resolved); fell back to text search, which found the editor as the only real-time consumer |

## Analysis limitations
- Static reading only: no build, no GPU tests, no RenderDoc capture and no validation-layer run (forbidden by the analysis rules). Hazard findings (RND-004, RND-015, RND-016) are reasoned from the Vulkan spec, not observed.
- The `query` keyword index (BM25) was weak and the top clusters and processes are third-party build-tree code, so I picked files from the first-party file list (call 11) and followed calls by reading code.
- Virtual calls through `IDevice`/`ICommandList`/`ISwapchain` and calls through lambdas are not resolved by the graph (calls 7, 9, 12), so I confirmed callers with literal text search.
- `modules/destruction` (the reference render feature and the main dynamic-geometry consumer) is gitignored and not indexed. I could not see how often it calls `context.upload` per break. RND-002 and RND-003 are rated from the engine contract it must use.
- There is no runtime game renderer to check: `GameSession` does not render, and only the editor drives `ForwardRenderer`.

## Cross-discipline notes
- HLSL: `Particles.cs.hlsl` / `Particle.vs.hlsl` process the full ring twice per frame (RND-018). Check dead-particle rejection cost and whether an alive-list compaction is wanted.
- HLSL: the `FrameData`/`ObjectData` layouts are mirrored by hand between `ForwardRenderer.cpp:31-76` and `ForwardCommon.hlsli` (only `static_assert` on sizes). A shared generated header would remove drift risk.
- Performance-Optimization: `ForwardRenderer::AddPasses` computes `Transpose(Inverse(transform))` per object per frame on the frame thread (ForwardRenderer.cpp:283-302), up to 16384 objects in the editor.
- Game-Engine-Design: no standalone game player renders. `GameSession` is headless and `EditorRenderer` is the only render loop, so runtime frame pacing and swapchain handling exist only in tools code.
- Unit-Tests: `EditorRenderer` (acquire/present error paths, mesh-pool reset) has no tests. Swapchain tests cover headless acquire/resize/zero-size only (`tests/rhi_vulkan/SwapchainTests.cpp:45-79`).
