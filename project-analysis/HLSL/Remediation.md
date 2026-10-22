# HLSL — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. HLSL-001, HLSL-002, HLSL-006: cheap safety nets on the CPU/GPU contract. They turn silent GPU misreads into compile-time or pipeline-creation errors, and they make every later shader change safer.
2. HLSL-005: a one-line validation that closes a GPU data race.
3. HLSL-003, HLSL-004: the largest GPU performance wins. Measure them with the `linux-clang-profile` preset and a GPU capture before and after.
4. HLSL-008, HLSL-009: small numerical and colour fixes. Regenerate the golden images afterwards (`INTERI_UPDATE_GOLDEN=1`, then inspect the images, then let CI's lavapipe confirm).
5. HLSL-007, HLSL-010, HLSL-011, HLSL-012: tooling, performance and portability hardening. Do these when touching those areas or when a console/D3D12 backend starts.

## HLSL-001 — CPU/GPU scene-buffer layouts are mirrored by hand and only their total size is asserted
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Choose one source of truth. Option A (smallest change): add an `offsetof` `static_assert` in `engine/render/src/ForwardRenderer.cpp` for every field the HLSL reads (`FrameData::cascades == 192`, `cascadeSplits == 448`, `lightCount == 480`, ..., `ObjectData::flags == 188`), and the same in `Particles.cpp` (`FrameHeader`, plus `Particle` in `Particles.h`) and `Decals.cpp` (`DecalHeader`, `DecalData`). Option B (preferred long term): a shared header `engine/render/shaders/ForwardLayout.h(lsli)` using the `#ifdef __cplusplus` pattern that defines the offsets once as `static const uint`/`inline constexpr std::uint32_t`. Both sides include it, and the C++ asserts `offsetof(...) == kFrameX`.
  2. Replace `kFrameCascadesOffset = 192` with `offsetof(FrameData, cascades)`.
  3. Pin the cascade count: add `static_assert(kMaxShadowCascades == 4, "ForwardCommon.hlsli loads splits/texels as float4 and cascades as float4x4[4]")` next to `FrameData`, or emit a `#define INTERI_MAX_SHADOW_CASCADES` from the shared header.
- **Illustrative change (not applied):**
  ```cpp
  static_assert(sizeof(FrameData) == 528);
  static_assert(offsetof(FrameData, cascades) == 192);       // kFrameCascades
  static_assert(offsetof(FrameData, cascadeSplits) == 448);  // kFrameCascadeSplits
  static_assert(offsetof(FrameData, lightCount) == 480);     // kFrameLightCount
  static_assert(offsetof(FrameData, cascadeCount) == 512);   // kFrameCascadeCount
  static_assert(offsetof(ObjectData, albedoTexture) == 148); // kObjectAlbedoTexture
  static_assert(offsetof(ObjectData, firstIndex) == 176);    // kObjectDraw
  static_assert(kMaxShadowCascades == 4, "HLSL reads 4 cascades");
  ```
  (`offsetof` on these standard-layout structs is fine. `Mat4`/`Vec3` must stay standard-layout. If clang-tidy complains, `std::bit_cast`-free alternatives exist.)
- **Verification:** Swap two same-sized fields in `FrameData` locally. The build must fail. The golden and GPU tests (`ctest --preset linux-clang-debug -L 'integration|golden'`) still pass on the unchanged layout.
- **Risks / trade-offs:** None at run time. Option B touches every shader include but removes duplication for good.

## HLSL-002 — Reflected push-constant size and workgroup size are discarded; push uploads are never checked against the shader
- **Severity / Effort:** Medium / M
- **Depends on:** None (pairs well with HLSL-006)
- **Steps:**
  1. Store the reflection result in `PipelineData` (`engine/rhi_vulkan/src/VulkanImpl.h:87`): `pushConstantSize` (the maximum over the VS and PS for graphics pipelines) and `workgroupSize`.
  2. In `VulkanCommandList`, remember the bound pipeline's `pushConstantSize` in `BindPipeline`. In `PushConstants`, add `INTERI_ASSERT(data.size() >= m_boundPushSize, ...)`, and in `Dispatch`/`Draw*` assert that a push of the required size happened since the bind (asserts only, so no shipping cost).
  3. Add `static_assert(sizeof(X) == N)` next to each C++ push mirror, with a comment naming the HLSL struct: `ForwardPush` 32, `ShadowPush` 16, `CullPush` 36, `DepthOnlyPush` 8, `HiZPush` 32, `CullingPush` 8, particle `ComputePush` 12 / `DrawPush` 16, `DecalPush` 8, `ImGuiPush` 32.
  4. Optionally expose `IDevice::WorkgroupSize(PipelineHandle)` so callers compute group counts from the shader rather than a duplicated literal 64 or 8. At minimum, assert the expected size in a GPU test.
  5. Add the new device query to `tests/rhi/MockDevice.h` and `NullDevice.h` in the same change (CLAUDE.md rule).
- **Illustrative change (not applied):**
  ```cpp
  struct PipelineData { VkPipeline pipeline; VkPipelineBindPoint bindPoint; std::uint32_t pushConstantSize = 0;
                        std::array<std::uint32_t, 3> workgroupSize{}; };
  // CreateGraphicsPipeline:
  data.pushConstantSize = std::max(vertex.pushConstantSize, pixel.pushConstantSize);
  ```
- **Verification:** A new `tests/rhi_vulkan` test that binds the `Cull` pipeline and pushes 32 bytes must trip the assert (with `ScopedAssertHandler`, skipping when asserts are disabled). The existing GPU tests pass.
- **Risks / trade-offs:** `PushConstants` before `BindPipeline` is legal in Vulkan with a shared layout. Make sure no existing call site relies on that order (none was found in the renderer).

## HLSL-003 — Opaque forward pixel shader contains `discard`, so no opaque draw benefits from early depth rejection
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Split the forward pipeline into two: `Forward` (no `discard`; compile `Forward.ps.hlsl` with `#define INTERI_DITHER 0`, or move the dither test into a separate `ForwardDither.ps.hlsl` that includes the common body) and `ForwardDither` (as today).
  2. In `Cull.cs.hlsl`, write objects with `opacity < 1` into a separate indirect list, or add an `kObjectDithered` flag and a second culling view. Draw the opaque list first, then the dithered list.
  3. Optionally add `[earlydepthstencil]` to the non-dither variant. Better still, consider a full main-view depth prepass (re-using `DepthOnly.vs`) followed by the forward pass with `depthCompare = Equal` and depth write off, which also removes overdraw shading.
- **Illustrative change (not applied):**
  ```hlsl
  #if INTERI_DITHER
      if (opacity < 1.0 && opacity < DitherThreshold(input.position.xy)) { discard; }
  #endif
  ```
  `interi_add_shaders` would need a `DEFINES` argument (or a second source file that sets the define and includes the body).
- **Verification:** The golden images stay identical (the outputs are the same). In a GPU capture (RenderDoc/RGP/Nsight) of a dense scene, the forward pass reports early-Z kills and fewer PS invocations. Record the forward-pass GPU time before and after with the profile preset.
- **Risks / trade-offs:** One more pipeline and indirect list. A depth prepass adds vertex cost, which pays off only with overdraw. Measure first.

## HLSL-004 — Particle draws process the whole ring twice per frame, whatever the live count
- **Severity / Effort:** Medium / L
- **Depends on:** HLSL-005 (touches the same compute shader)
- **Steps:**
  1. In `Particles.cs.hlsl` mode 1, after integrating, append each live particle's index to one of two compacted index lists (alpha / additive) with `InterlockedAdd` on a counter. Write `DrawIndirect` arguments (`vertexCount = count * 6`) for each list.
  2. Change `Particle.vs.hlsl` to read `particleIndex = list[vertexId / 6]`, and replace the two `Draw(capacity * 6)` calls with `DrawIndirect` from those arguments (`Particles.cpp:224-234`). This removes the off-screen `float4(2,2,2,1)` path.
  3. For alpha particles, sort by view depth back to front when blending artifacts matter (a GPU bitonic or radix sort over the compacted list). Alternatively, document that alpha particles are order-independent-ish (soft, low alpha) and keep ring order.
  4. Update `IntegrateParticles` and the GPU tests only if the simulation order changes (it doesn't need to: compaction runs after integration).
- **Verification:** `ParticleGpuTests` still pass. With 0 live particles the particle pass issues 0 vertex invocations (check pipeline statistics or a capture). Frame time with capacity 65,536 and few live particles drops measurably.
- **Risks / trade-offs:** Atomic append order is nondeterministic. That only changes draw order (blending order for alpha), so pair it with a sort or accept it for additive particles only.

## HLSL-005 — Emit dispatch can make two threads write the same ring slot when `maxEmitPerFrame > capacity`
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `GpuParticleSystem::Create` (`engine/render/src/Particles.cpp:83`), reject the configuration at run time rather than with an assert only: return `MakeError(ErrorCode::InvalidArgument, "maxEmitPerFrame must not exceed capacity")` when `maxEmitPerFrame > capacity` or `capacity == 0`. Put the check before the existing `INTERI_ASSERT` (and drop those terms from the assert) so a debug test sees the error instead of an assert.
  2. Keep the valid path unchanged: configurations with `0 < maxEmitPerFrame <= capacity` create the system exactly as today. Clamping in `Emit` is not a substitute, because the invalid configuration would still be accepted (and the per-frame upload buffer sized from it).
  3. Document the constraint on `ParticleSettings` (`Particles.h:54-55`).
- **Illustrative change (not applied):**
  ```cpp
  if (settings.capacity == 0 || settings.maxEmitPerFrame > settings.capacity)
  {
      return MakeError(ErrorCode::InvalidArgument, "particle capacity must be >= maxEmitPerFrame > 0");
  }
  ```
- **Verification:** Unit tests that create the system with `capacity = 8, maxEmitPerFrame = 16` and with `capacity = 0` get `ErrorCode::InvalidArgument` (in builds with asserts compiled out too). Existing tests, which use valid settings, are unchanged.
- **Risks / trade-offs:** None. The default settings already satisfy the constraint.

## HLSL-006 — Shader reflection checks only set/binding numbers, not descriptor types, so a stray `cbuffer` or a wrong resource type passes
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `ReflectSpirv` (`engine/rhi_vulkan/src/ShaderReflection.cpp:79-88`), map each binding number to its expected `SpvReflectDescriptorType`: 0 → `STORAGE_BUFFER`, 1 → `SAMPLED_IMAGE`, 2 → `SAMPLER`, 3 → `STORAGE_IMAGE`. Reject mismatches with a message naming the resource and pointing to `Bindless.hlsli` ("use push constants and the bindless tables; cbuffers are not supported").
  2. Share the binding constants (`kBindlessBuffers` etc.) between `VulkanDevice.cpp` and `ShaderReflection.cpp` so the table definition is single-sourced on the C++ side.
  3. Add a test shader to `tests/rhi_vulkan/shaders` with a `cbuffer` (and one with `Texture2D` at binding 0), and expect `ReflectSpirv` to fail.
- **Illustrative change (not applied):**
  ```cpp
  constexpr std::array kExpected{SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                 SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER, SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE};
  if (binding.set != 0 || binding.binding > 3 || binding.descriptor_type != kExpected[binding.binding]) { ... }
  ```
- **Verification:** The new reflection tests fail before and pass after the change. Every existing pipeline still creates (the GPU tests pass).
- **Risks / trade-offs:** Check how spirv-reflect reports `RWByteAddressBuffer` (storage buffer) and `RWTexture2D` with `image_format` (storage image), so that existing shaders aren't rejected.

## HLSL-007 — Shader build has one configuration: always `-O3`, no debug info, no shader hot reload
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. In `cmake/InteriShaders.cmake`, add `$<$<CONFIG:Debug>:-Od -Zi -fspv-debug=vulkan-with-source>` (or an `INTERI_SHADER_DEBUG` option, ON in debug presets), and keep `-O3` for release/profile.
  2. Optionally emit the `.spv` next to the embedded header and add a development-only loader that, when a `--shader-dir` flag is set, reads `.spv` files and rebuilds pipelines. That would mean `FileWatcher` reuse plus a pipeline-recreate path in `ForwardRenderer`/`GpuParticleSystem`/`DecalRenderer`.
  3. Consider DXC `-MD -MF` depfiles (`DEPFILE` in `add_custom_command`, supported by Ninja) instead of globbing every `.hlsli`.
- **Verification:** A RenderDoc capture of the debug build shows HLSL source for the forward pass. Release SPIR-V is byte-identical to today's.
- **Risks / trade-offs:** Debug SPIR-V is larger and slower. Lavapipe golden tests should still run on the optimized variant if their tolerances are tight.

## HLSL-008 — NaN and Inf sources in the lighting path on degenerate inputs
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Point lights (`Forward.ps.hlsl:151-156`): use `max(distanceSquared, 1e-8)` in the `rsqrt` and the ratio, and `max(positionRadius.w * positionRadius.w, 1e-8)` for the radius.
  2. Half vector (`:30`): `const float3 h = normalize(v + l + float3(0, 1e-6, 0))`, or compute `h` from `safeNormalize` (returns `n` when the length is ~0).
  3. Normal (`:126`) and triplanar weights (`:92-93`): `normalize` via a safe helper, and `weights /= max(weights.x + weights.y + weights.z, 1e-6)`.
  4. Put the helpers (`SafeNormalize`) in a shared `.hlsli` (for example `engine/shaders/include/interi/Math.hlsli`).
- **Verification:** A new GPU test renders a quad with a point light at the shaded surface and a zero-length normal object. `isnan` checks on the readback pass. The golden images change only within tolerance (or not at all).
- **Risks / trade-offs:** The epsilons can change goldens by at most 1 LSB. Regenerate and inspect them.

## HLSL-009 — Approximate `pow(x, 2.2)` sRGB decode where the targets use the exact sRGB curve
- **Severity / Effort:** Low / S
- **Depends on:** HLSL-008 (shares the new math include)
- **Steps:**
  1. Add `float3 SrgbToLinear(float3 c)` with the piecewise IEC 61966-2-1 curve (`c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)`) to a shared `.hlsli`.
  2. Use it in `ImGui.vs.hlsl:14` and `Particle.vs.hlsl:29`.
- **Verification:** An ImGui headless GPU test that draws a vertex colour of `(10,10,10)` into an sRGB target reads back `(10,10,10)` ± 1. Particle goldens are regenerated and inspected.
- **Risks / trade-offs:** Golden images change slightly. The editor's look shifts in dark tones, towards what ImGui intends.

## HLSL-010 — The bindless buffer table is writable-only (`RWByteAddressBuffer`), even for read-only vertex and scene loads
- **Severity / Effort:** Low / M
- **Depends on:** HLSL-006 (reflection type check must accept the alias)
- **Steps:**
  1. In `Bindless.hlsli`, add `[[vk::binding(0, 0)]] ByteAddressBuffer g_ReadBuffers[];` aliasing binding 0, as is already done for textures and samplers. DXC emits `NonWritable` for it.
  2. Switch read-only loads (vertex pulling, `LoadMat4`, `LoadFloat`/`LoadUint`, `LoadParticle`, decal reads) to `g_ReadBuffers`, and keep `g_Buffers` for stores and atomics (Cull, LightCulling, Particles.cs, HiZ via storage images).
  3. Once no graphics stage writes buffers, drop `vertexPipelineStoresAndAtomics`/`fragmentStoresAndAtomics` from `Require()` if no shader needs them.
- **Verification:** SPIR-V disassembly (`spirv-dis`) of `Forward.vs` shows `NonWritable` on the buffer variable used. Validation-clean GPU tests pass.
- **Risks / trade-offs:** Aliasing two declarations on one binding is already a project convention. Watch the validation layer's descriptor-type checks.

## HLSL-011 — Clustered light culling transforms every light into view space once per cluster
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Transform the lights to view space once: on the CPU (write view-space positions alongside the world ones in the scene buffer), or in a small compute pre-pass, or per thread group into `groupshared` batches of 64 lights (load and transform once per group, `GroupMemoryBarrierWithGroupSync` in uniform flow, test all clusters of the group).
  2. Count overflow: keep the clamp at 32, but also write a per-frame "clusters overflowed" counter (`InterlockedAdd`) that a debug overlay or log can read. Keep the CPU reference `AssignLights` in sync.
- **Verification:** `ClusteredLightingGpuTests` still match the CPU reference. GPU time of `LightCulling` with 1,024 lights drops.
- **Risks / trade-offs:** The groupshared variant changes nothing observable. The CPU pre-transform changes the scene-buffer layout (see HLSL-001).

## HLSL-012 — Shader sources are SPIR-V-only: Vulkan attributes carry semantics that a DXIL/console compile would lose
- **Severity / Effort:** Low / L
- **Depends on:** HLSL-001 (shared layout header simplifies the port)
- **Steps:**
  1. Introduce `engine/shaders/include/interi/Platform.hlsli` with macros: `INTERI_PUSH_CONSTANT(Type, name)` (`[[vk::push_constant]]` on Vulkan, `ConstantBuffer<Type> name : register(b0, space0)` / root constants on D3D12), `INTERI_BINDLESS(...)` (`[[vk::binding]]` versus `register(tN, spaceN)` or SM 6.6 `ResourceDescriptorHeap`), and `INTERI_BASE_INSTANCE` (`[[vk::builtin("BaseInstance")]]` versus `SV_StartInstanceLocation` on SM 6.8, or a root constant).
  2. Convert the shaders to the macros without changing the SPIR-V output (verify byte-identical `.spv`).
  3. Optionally add a CI job that compiles every shader to DXIL with DXC (`-T *_6_6`, no `-spirv`) to keep both paths compiling.
- **Verification:** The SPIR-V is byte-identical before and after. A DXIL compile of every shader succeeds with `-WX`.
- **Risks / trade-offs:** The macros add indirection to simple shaders. The console repos may prefer their own mapping, so coordinate the macro names with them.

## HLSL-013 — `NonUniformResourceIndex` usage is correct (positive observation)
Info only. No remediation required. Keep the rule as a review checklist item (see Goal.md).

## HLSL-014 — GPU particle integration may be contracted to FMA, so CPU parity holds only to a tolerance (by design)
Info only. No remediation required while particles are visual-only. If GPU particle state ever feeds simulation, mark the integration arithmetic `precise`.
