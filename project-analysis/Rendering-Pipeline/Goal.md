# Rendering-Pipeline — Goal State

## Vision
Interi renders through a backend-neutral RHI chosen by a factory. The Vulkan backend is clean under validation and synchronization validation in tests and in the debug editor, and it detects, reports and survives device and surface loss. The frame graph labels and times every pass, rejects ambiguous accesses, and keeps frames in flight overlapped. Dynamic geometry (imported meshes, module render features and destruction debris) reaches the GPU through the frame's own upload ring into a sub-allocated, reclaimable mesh pool, with no CPU/GPU drain during play. The fully dynamic occlusion and shadowing that already survives destruction stays that way.

## Acceptance criteria
- [ ] AC-1 No `INTERI_VK_CHECK` (assert-only check) remains on `vkQueueSubmit2`, `vkWaitSemaphores`, `vkGetSemaphoreCounterValue`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR`, `vkAllocateCommandBuffers` or `vkBegin/EndCommandBuffer`. `VK_ERROR_DEVICE_LOST` maps to a dedicated `ErrorCode`, `IDevice` reports the lost state, and a MockDevice test shows that `ResourceManager`/`CommandListPool`/`UploadQueue` destructors return without hanging after loss. (resolves RND-001, RND-017)
- [ ] AC-2 In the editor, a lost device or surface produces a logged report (device name, timeline values, last pass list) and an autosave instead of an abort or a silent loop. (resolves RND-001)
- [ ] AC-3 During steady-state play and editing, `IDevice::WaitForValue` is called only by `CommandListPool::BeginFrame` (frame-in-flight pacing). A test or counter shows 0 extra waits per frame while a render feature uploads new meshes every frame. (resolves RND-002)
- [ ] AC-4 `RenderFeatureContext::upload` records into the current frame's command list, and its documentation no longer says "submits it and waits". The module ABI version is bumped and the Destruction Module builds against it. (resolves RND-002)
- [ ] AC-5 `MeshPool` supports `Free` with timeline-retired reuse. A 1000-frame GPU test that adds and frees debris meshes keeps pool usage bounded with zero validation errors, and `EditorRenderer::ResetMeshPool` is not called in that scenario. (resolves RND-003)
- [ ] AC-6 When the pool-full fallback triggers, editor meshes are still drawn in that frame (no one-frame disappearance), as verified by an editor-renderer test. (resolves RND-003)
- [ ] AC-7 After a successful `AcquireNextImage`, every exit path of `EditorRenderer::Render` submits and presents (or abandons) the image. A test with a failing pass runs four frames with no assert and no validation error. (resolves RND-004)
- [ ] AC-8 `ForwardRenderer`, `DecalRenderer` and `GpuParticleSystem` clamp or reject inputs over capacity in all build types, and they refuse (assert or wait) to rewrite a per-frame slot whose previous timeline value has not completed. Unit tests cover both. (resolves RND-005)
- [ ] AC-9 Graphics and compute pipelines are created through a `VkPipelineCache` that is saved on exit and reloaded at start. A test shows a non-empty blob that is accepted on reload. (resolves RND-006)
- [ ] AC-10 Every executed frame-graph pass is wrapped in a debug label that is visible in RenderDoc, per-pass GPU timings are available in the editor, and a stats view shows visible draws per view, `MeshPool` usage and the VMA heap budget. (resolves RND-007, RND-011)
- [ ] AC-11 `render::CreateTexture` validates every mip against the upload chunk before recording, and never calls `IDevice::DestroyTexture` on a texture referenced by an unsubmitted command list (MockDevice test). (resolves RND-008)
- [ ] AC-12 The acquire semaphore is waited on at `COLOR_ATTACHMENT_OUTPUT`, and the swapchain image's first barrier uses that source stage. Swapchain and GPU tests are clean under sync validation. (resolves RND-009)
- [ ] AC-13 Recording a texture barrier, attachment or bindless-index lookup performs no heap allocation, measured by a benchmark with a counting allocator. (resolves RND-010)
- [ ] AC-14 `UploadQueue` releases idle chunks above a configured cache size (unit test). (resolves RND-011)
- [ ] AC-15 `tools/editor` obtains its device and swapchain through a backend-neutral factory, and only one backend-selection TU includes `interi/rhi_vulkan/VulkanDevice.h`. (resolves RND-012)
- [ ] AC-16 Debug builds of `level_editor` enable validation by default when the layer is installed, and the `--frames N` smoke run fails on any validation error. (resolves RND-013)
- [ ] AC-17 Editor scene meshes are uploaded indexed (welded per slot). Vertex count for a generated sphere equals its unique (position, normal) count, and goldens are unchanged. (resolves RND-014)
- [ ] AC-18 Swapchain recreation retires old swapchains and semaphores by present fence (`VK_EXT_swapchain_maintenance1`) or by a frame-delayed retire list, with no `vkQueueWaitIdle` per resize. A 100-resize test is validation-clean. (resolves RND-015)
- [ ] AC-19 `FrameGraph::Compile` rejects a pass that accesses one resource in two different states (FrameGraphTests case). (resolves RND-016)
- [ ] AC-20 Particle draws and simulation scale with live particles through an alive list and indirect arguments. Existing particle GPU and golden tests pass. (resolves RND-018)
- [ ] AC-21 Hi-Z occlusion and shadow maps remain rebuilt from current-frame geometry. No PVS, baked occlusion, lightmaps or uninvalidated shadow cache is introduced for destructible content. (preserves RND-019)
- [ ] AC-22 When the UV/tangent vertex stream lands, triplanar mapping remains available for fracture interior faces. (tracks RND-020)

## Guardrails to keep it that way
- GPU tests (`integration`/`golden` labels) keep validation and synchronization validation on, and fail on any error (existing). Extend this to the editor smoke run (`level_editor --frames 120`).
- Add a CI grep check that `INTERI_VK_CHECK` is not applied to submit, wait, acquire or present calls in `engine/rhi_vulkan/src`.
- Every new `IDevice`/`ICommandList` virtual (markers, timestamps, swapchain factory, lost state) comes with a `MOCK_METHOD` and a `NullDevice` override in the same change (existing project rule).
- Review checklist for render code: no `WaitForValue`/`vkQueueWaitIdle` outside frame pacing, shutdown, resize and explicit load paths; per-frame buffers indexed by timeline-checked slots; capacity overflows clamped and logged, never only asserted.
- Benchmark `BM_` cases for barrier and attachment recording with an allocation counter; regressions reviewed with `tools/bench_compare`.
- A `FrameGraph::Compile` test suite covering misuse (read before write, conflicting states, culling).

## Out of scope
- HLSL shader implementation details (Hi-Z reduction, clustered lighting math, particle and decal shaders) belong to the HLSL analyst.
- A D3D12 backend, and the PS5/Xbox backends, which live in private repos. This discipline only requires the factory seam that lets them plug in.
- Async compute and multi-queue scheduling, mesh shaders, virtual geometry and texture streaming. These are good future work, but nothing in the current milestones depends on them.
- A standalone game player renderer (an engine-design decision). The criteria above apply to it once it exists.
