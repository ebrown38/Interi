# HLSL — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** All 26 tracked shader files (19 `.hlsl`, 7 `.hlsli`, 1,097 lines): `engine/shaders/include/interi/Bindless.hlsli`, `engine/render/shaders/*` (culling, Hi-Z, clustered light culling, shadows, PBR forward, particles, decals), `engine/editor_ui/shaders/*` (ImGui), and the test shaders under `tests/render/shaders` and `tests/rhi_vulkan/shaders`. The shader build (`cmake/InteriShaders.cmake`, `cmake/EmbedSpirv.cmake`), and the C++ side of the CPU/GPU contract: `engine/rhi_vulkan/src/ShaderReflection.cpp`, `engine/rhi_vulkan/src/VulkanDevice.cpp` (bindless set, pipeline layout, push constants, pipeline creation, samplers, device features), `engine/rhi/include/interi/rhi/{Types.h,Device.h}`, `engine/render/src/{ForwardRenderer.cpp,Particles.cpp,Decals.cpp}`, `engine/render/include/interi/render/{Clusters.h,Particles.h,Shadows.h}`, `engine/editor_ui/src/ImGuiRenderer.cpp`. `modules/` (destruction and physics, not indexed and separate projects) was not analyzed.
**Summary:** 14 issues — Critical: 0 · High: 0 · Medium: 6 · Low: 6 · Info: 2

Overall the shader code is in good shape. It is compiled offline by DXC (SM 6.6, `-HV 2021`, `-WX`) to SPIR-V and embedded. One bindless set is validated by reflection. `NonUniformResourceIndex` is used where an index really is divergent. No wave intrinsics are used, so the code does not depend on wave size. The only groupshared code (a test) keeps its barriers in uniform control flow. Reverse-Z is consistent in every pass, and the frustum-plane extraction correctly skips the degenerate far plane of the infinite projection. No Critical or High issues were found. The findings below are missing safeguards on the CPU/GPU contract, performance patterns, and minor numerical or portability points.

## Findings

### HLSL-001 — CPU/GPU scene-buffer layouts are mirrored by hand and only their total size is asserted
- **Severity:** Medium
- **Confidence:** High
- **Category:** CPU/GPU layout contract
- **Location:** `engine/render/src/ForwardRenderer.cpp:30-76`, `engine/render/src/ForwardRenderer.cpp:131`, `engine/render/shaders/ForwardCommon.hlsli:10-42`, `engine/render/shaders/Forward.ps.hlsl:53-54`, `engine/render/src/Particles.cpp:20-33`, `engine/render/shaders/ParticleCommon.hlsli:9-16`, `engine/render/src/Decals.cpp:22-42`, `engine/render/shaders/DecalCommon.hlsli:8-16`
- **Evidence:** The forward renderer's `FrameData`/`ObjectData` structs are mirrored in HLSL as about 30 hand-written byte-offset constants (`kFrameCascades = 192`, `kFrameCascadeSplits = 448`, `kFrameLightCount = 480`, ... `kObjectDraw = 176`). The C++ side checks only `static_assert(sizeof(FrameData) == 528)` and `static_assert(sizeof(ObjectData) == 192)`; no field has an `offsetof` assertion. The C++ code also keeps a second copy of one offset, `constexpr std::uint32_t kFrameCascadesOffset = 192; // FrameData::cascades`. The HLSL hard-codes `kMaxShadowCascades == 4` (`float4x4[4]`, and `splits`/`texels` are loaded as `float4`), while the C++ code derives these sizes from `kMaxShadowCascades` (`engine/render/include/interi/render/Shadows.h:13`). The particle header and the decal header/record follow the same pattern: size-only `static_assert`s plus hand-written HLSL offsets.
- **Impact:** Reordering two same-sized fields (for example swapping `roughness` and `textureScale`, or `screenWidth` and `clusterNear`), or adding a field and removing a pad, passes every `static_assert` and compiles cleanly, but the GPU reads the wrong data. Shading or culling then breaks silently, and only the golden-image tests on lavapipe would catch it, and only if those paths are covered. Raising `kMaxShadowCascades` changes the size, but the HLSL still reads 4 splits. The CLAUDE.md rule "the C++ and HLSL layouts must match" is enforced only by review.

### HLSL-002 — Reflected push-constant size and workgroup size are discarded; push uploads are never checked against the shader
- **Severity:** Medium
- **Confidence:** High
- **Category:** Resource binding / push constants
- **Location:** `engine/rhi_vulkan/src/ShaderReflection.cpp:68-77`, `engine/rhi_vulkan/include/interi/rhi_vulkan/ShaderReflection.h:16-23`, `engine/rhi_vulkan/src/VulkanImpl.h:87-91`, `engine/rhi_vulkan/src/VulkanDevice.cpp:497-502`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1413-1449`, `engine/render/src/ForwardRenderer.cpp:80-137`
- **Evidence:** `ReflectSpirv` computes `pushConstantSize` and `workgroupSize`, but `CreateComputePipeline` and `CreateGraphicsPipeline` use the result only to check the stage. `PipelineData` stores just `{pipeline, bindPoint}`. `VulkanCommandList::PushConstants` asserts only `data.size() <= kMaxPushConstantSize && data.size() % 4 == 0` and always pushes at offset 0 into the shared 128-byte range. None of the ten C++ push mirrors has a `sizeof` check against its HLSL struct: `ForwardPush`, `ShadowPush`, `CullPush` (36 bytes), `DepthOnlyPush`, `HiZPush`, `CullingPush`, the particle `ComputePush`/`DrawPush`, `DecalPush` and `ImGuiPush`. `impact` on `ICommandList::PushConstants`: one override (`VulkanCommandList`). The template overload (`Device.h:70-75`) checks only the 128-byte limit.
- **Impact:** If a C++ push struct ends up shorter than the HLSL block (a field added only on the shader side), the shader reads whatever bytes the previous pipeline pushed. That is a silent wrong-index bug that can make a bindless access go out of bounds. The shared range is `VK_SHADER_STAGE_ALL` × 128 bytes, so the Vulkan validation layer does not report the partial update. The C++ dispatch math (`(objectCount + 63) / 64`, `(w + 7) / 8`) also duplicates each shader's `numthreads` without a check against the reflected `workgroupSize`.

### HLSL-003 — Opaque forward pixel shader contains `discard`, so no opaque draw benefits from early depth rejection
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** GPU performance
- **Location:** `engine/render/shaders/Forward.ps.hlsl:112-116`, `engine/render/src/ForwardRenderer.cpp:163-167`, `engine/render/src/ForwardRenderer.cpp:534-571`
- **Evidence:** The single forward pipeline, with depth write enabled and compare `Greater`, uses one pixel shader for every object, and that shader starts with the screen-door test:
  ```hlsl
  const float opacity = LoadFloat(scene, object + kObjectOpacity);
  if (opacity < 1.0 && opacity < DitherThreshold(input.position.xy))
  {
      discard;
  }
  ```
  There is no depth prepass for the main view: the occluder depth pass writes a separate `Occluder depth` texture used only for Hi-Z. The lit result therefore depends on late depth testing. Each covered fragment runs the full PBR, cascaded-shadow PCF (9 compare samples), triplanar sampling (3 samples) and clustered point-light loop before its depth is resolved.
- **Impact:** With `discard` in a depth-writing shader, most hardware falls back to late-Z, or to a re-Z mode that cannot kill fragments before shading. Overdraw then costs a full shading invocation per layer, even though only fading debris uses `opacity < 1`. On the targeted console-class GPUs this is the largest avoidable per-pixel cost in the frame. (Inferred from the shader and pipeline structure; not profiled.)

### HLSL-004 — Particle draws process the whole ring twice per frame, whatever the live count
- **Severity:** Medium
- **Confidence:** High
- **Category:** GPU performance
- **Location:** `engine/render/shaders/Particle.vs.hlsl:9-22`, `engine/render/src/Particles.cpp:224-234`, `engine/render/include/interi/render/Particles.h:54`
- **Evidence:** `commands.Draw(capacity * 6, 1, 0, 0)` is issued twice, once with the alpha pipeline and once with the additive one. Every vertex loads its 64-byte particle (`LoadParticle`) and emits `float4(2, 2, 2, 1)` for free slots and for particles of the other blend mode. The default `capacity` is 65,536, so each frame runs 786,432 vertex invocations and 3 × 64-byte buffer loads per invocation, plus primitive setup for 262,144 mostly degenerate triangles, even when no particles are alive. Alpha-blended particles are drawn in ring order with no back-to-front sort.
- **Impact:** The fixed cost scales with capacity, not with live particles. The vertex and primitive-assembly load is significant on lower-end GPUs and wastes budget on consoles. Unsorted alpha blending gives order-dependent artifacts where smoke particles overlap.

### HLSL-005 — Emit dispatch can make two threads write the same ring slot when `maxEmitPerFrame > capacity`
- **Severity:** Medium
- **Confidence:** High
- **Category:** Compute correctness / data race
- **Location:** `engine/render/shaders/Particles.cs.hlsl:18-31`, `engine/render/src/Particles.cpp:83`, `engine/render/src/Particles.cpp:149-168`, `engine/render/include/interi/render/Particles.h:54-55`
- **Evidence:** Mode 0 maps emitted particle `id.x` to slot `(emitStart + id.x) % capacity` and stores four `uint4`s. `Emit` accepts up to `maxEmitPerFrame` particles per frame, and `Create` asserts only `capacity > 0 && maxEmitPerFrame > 0`. Nothing requires `maxEmitPerFrame <= capacity`. If a frame emits more particles than `capacity`, slots are written by several threads in the same dispatch. The `% capacity` also relies on an assert (compiled out under `INTERI_SHIPPING`) to exclude `capacity == 0`, which would make the modulo undefined on the GPU.
- **Impact:** This is an unsynchronized write-write race: the surviving particle (and even a mix of 16-byte chunks from different particles) depends on the GPU's scheduling. The result breaks the "same operations, same order as `IntegrateParticles`" contract and the tolerance-based CPU/GPU comparison for such configurations. It is only reachable with a non-default configuration, hence Medium.

### HLSL-006 — Shader reflection checks only set/binding numbers, not descriptor types, so a stray `cbuffer` or a wrong resource type passes
- **Severity:** Medium
- **Confidence:** High
- **Category:** Resource binding validation
- **Location:** `engine/rhi_vulkan/src/ShaderReflection.cpp:79-88`, `engine/rhi_vulkan/src/VulkanDevice.cpp:687-704`, `engine/shaders/include/interi/Bindless.hlsli:7-12`
- **Evidence:** `ReflectSpirv` rejects a binding only when `binding.set != 0 || binding.binding > 3`. It does not compare `binding.descriptor_type` with the table's type (0 = `STORAGE_BUFFER`, 1 = `SAMPLED_IMAGE`, 2 = `SAMPLER`, 3 = `STORAGE_IMAGE`). With `-spirv`, DXC assigns an un-annotated `cbuffer`/`ConstantBuffer` or `Texture2D` to set 0 from binding 0 upwards. Such a declaration would land on a bindless binding with a different descriptor type and still pass reflection.
- **Impact:** A contributor (or a module author writing a render feature) who adds an ordinary `cbuffer` gets no clear `ReflectSpirv` error. Instead, pipeline creation fails or, without validation layers, the shader reads a mistyped descriptor (undefined behavior). The descriptor model is "the bindless tables only", so the reflection check should enforce the types too. Reflection already exists for exactly this purpose.

### HLSL-007 — Shader build has one configuration: always `-O3`, no debug info, no shader hot reload
- **Severity:** Low
- **Confidence:** High
- **Category:** Toolchain / iteration
- **Location:** `cmake/InteriShaders.cmake:55-60`
- **Evidence:** The DXC command line is fixed: `-spirv -T ${stage}_6_6 -E main -HV 2021 -fspv-target-env=vulkan1.3 -fvk-use-scalar-layout -O3 -WX`. Debug presets get no `-Zi`/`-fspv-debug=vulkan-with-source` (or `-Od`) variant, and no stripped PDB-style artifact is kept. Shaders are embedded as `constexpr` arrays (`cmake/EmbedSpirv.cmake`), so any shader edit means rebuilding and relinking the library. Nothing reloads shaders at run time, unlike the engine's module hot reload.
- **Impact:** RenderDoc/Nsight shader debugging shows optimized SPIR-V without source, which slows down investigating GPU issues (culling, clustering, decals). Shader iteration needs a full C++ relink. Not a correctness problem.

### HLSL-008 — NaN and Inf sources in the lighting path on degenerate inputs
- **Severity:** Low
- **Confidence:** High
- **Category:** Numerical correctness
- **Location:** `engine/render/shaders/Forward.ps.hlsl:30`, `engine/render/shaders/Forward.ps.hlsl:92-93`, `engine/render/shaders/Forward.ps.hlsl:126`, `engine/render/shaders/Forward.ps.hlsl:151-156`
- **Evidence:**
  - `toLight * rsqrt(distanceSquared)` is Inf/NaN when a shaded point coincides with a light position.
  - `distanceSquared / (positionRadius.w * positionRadius.w)` is `0/0` for a zero-radius light at distance 0.
  - `normalize(v + l)` is NaN when `l == -v` (light exactly behind the surface as seen from the camera; `nDotL` is 0 there, but `0 * NaN` is still NaN).
  - `normalize(input.normal)` and the triplanar `weights /= weights.x + weights.y + weights.z` are NaN for a zero normal (degenerate triangles, or a zero-scale axis in `Transpose(Inverse(model))`).
- **Impact:** A NaN writes a black or garbage pixel, and blending or later post effects (none yet) can spread it. All cases are edge cases, but debris with degenerate or zero-scale transforms is a realistic source in a destruction-focused engine.

### HLSL-009 — Approximate `pow(x, 2.2)` sRGB decode where the targets use the exact sRGB curve
- **Severity:** Low
- **Confidence:** High
- **Category:** Colour space correctness
- **Location:** `engine/editor_ui/shaders/ImGui.vs.hlsl:12-15`, `engine/render/shaders/Particle.vs.hlsl:28-29`, `engine/render/include/interi/render/Particles.h:32`
- **Evidence:** ImGui vertex colours (sRGB-encoded) and particle colours (documented as "sRGB RGBA8") are linearized with `pow(color.rgb, 2.2)`. The sRGB render target re-encodes with the exact piecewise sRGB transfer function.
- **Impact:** Colours don't round-trip. Dark values are noticeably off (8-bit code 10 decodes to about 0.00080 instead of 0.00304, so it re-encodes near 3 rather than 10), which shifts dark ImGui theme colours and dark smoke particles. This is purely visual and small.

### HLSL-010 — The bindless buffer table is writable-only (`RWByteAddressBuffer`), even for read-only vertex and scene loads
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Resource binding / performance / portability
- **Location:** `engine/shaders/include/interi/Bindless.hlsli:7`, `engine/rhi_vulkan/src/VulkanDevice.cpp:127-131`
- **Evidence:** `[[vk::binding(0, 0)]] RWByteAddressBuffer g_Buffers[];` is the only buffer table. Vertex pulling, per-object and per-frame data, particle and decal reads therefore all go through storage-buffer descriptors without the SPIR-V `NonWritable` decoration. The device must enable `vertexPipelineStoresAndAtomics` and `fragmentStoresAndAtomics` for every pipeline, as the comment at `VulkanDevice.cpp:129` notes.
- **Impact:** Drivers can't route read-only loads through read-only or constant paths, or assume the data won't be aliased by writes within a draw. A D3D12/console backend has to bind every buffer as a UAV, which for D3D12 also means UAV state for buffers that are only read. A read-only `ByteAddressBuffer` alias on the same binding (allowed, as the textures and samplers already alias) would remove both costs. The impact is moderate and hardware-dependent (inferred).

### HLSL-011 — Clustered light culling transforms every light into view space once per cluster
- **Severity:** Low
- **Confidence:** High
- **Category:** GPU performance
- **Location:** `engine/render/shaders/LightCulling.cs.hlsl:46-61`, `engine/render/include/interi/render/ForwardRenderer.h:106`
- **Evidence:** Each of the 3,456 cluster threads loads `LoadMat4(scene, kFrameView)` and, for every light (up to `kMaxPointLights = 1024`), loads the light and computes `mul(view, float4(positionRadius.xyz, 1.0))`. At the limit that is about 3.5 million identical light transforms per frame. The loop also stops at 32 lights per cluster (`kMaxLightsPerCluster`) with no overflow signal, which the CPU reference mirrors.
- **Impact:** Redundant ALU work and loads that grow with the light count. Lights could be transformed once (a pre-pass, or `groupshared` batches per thread group). Silent truncation at 32 lights hides lights in dense clusters with no diagnostic.

### HLSL-012 — Shader sources are SPIR-V-only: Vulkan attributes carry semantics that a DXIL/console compile would lose
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Portability (D3D12 / console)
- **Location:** `engine/render/shaders/Forward.vs.hlsl:6`, `engine/render/shaders/DepthOnly.vs.hlsl:13`, `engine/render/shaders/Shadow.vs.hlsl:15`, `engine/shaders/include/interi/Bindless.hlsli:7-12`, `engine/render/shaders/ForwardPush.hlsli:19`
- **Evidence:** The object index comes from `[[vk::builtin("BaseInstance")]] uint objectIndex : BASE_INSTANCE`. Bindings and push constants use only `[[vk::binding]]`/`[[vk::push_constant]]`, with no `register(..., space)` declarations and no `#ifdef` for other backends. Compiled as DXIL, the `vk::` attributes are ignored and `BASE_INSTANCE` becomes an ordinary vertex input that the vertex-input-free pipelines never provide.
- **Impact:** The planned PS5/Xbox backends (private repos) and any D3D12 path will have to fork or macro-wrap every shader. Instance-base handling in particular needs `SV_StartInstanceLocation` (SM 6.8) or a root constant. This is a portability risk, not a bug on today's Vulkan target.

### HLSL-013 — `NonUniformResourceIndex` usage is correct (positive observation)
- **Severity:** Info
- **Confidence:** High
- **Category:** Bindless indexing
- **Location:** `engine/render/shaders/Forward.ps.hlsl:95-96`
- **Evidence:** The only per-object descriptor index, the albedo texture loaded from `ObjectData` through a `nointerpolation` object index, is wrapped in `NonUniformResourceIndex`. Every other bindless index (scene, clusters, shadow map, samplers, Hi-Z, particle and decal buffers, ImGui texture) comes from push constants and is uniform. The `NonUniformResourceIndex` on `g_Push.materialSampler` is unnecessary but harmless. The device enables `shaderSampledImageArrayNonUniformIndexing` and `shaderStorageBufferArrayNonUniformIndexing`. No action needed. Keep this rule when per-object samplers or buffers are added.
- **Impact:** None. Recorded so the property is preserved.

### HLSL-014 — GPU particle integration may be contracted to FMA, so CPU parity holds only to a tolerance (by design)
- **Severity:** Info
- **Confidence:** Medium
- **Category:** Numerical determinism
- **Location:** `engine/render/shaders/Particles.cs.hlsl:49-52`, `engine/render/src/Particles.cpp:55-68`
- **Evidence:** The shader repeats `IntegrateParticles`' operation order, but it is compiled with `-O3`, has no `precise`, and the driver compiler may fuse `velocity += g * s * dt` and `position += v * dt` into FMAs. The CPU side is built with `-ffp-contract=off`. CLAUDE.md documents that the GPU tests compare with a tolerance.
- **Impact:** None while particles stay visual-only. If GPU particle state ever feeds simulation or replays, mark the arithmetic `precise` or move it back to the CPU.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 552,120 edges, 4,973 communities, 614 processes; indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are third-party (Joystick 355, Hidapi 316, Profiler 159, Manifold 134, Vulkan 84, Tracy 83 ...) plus unnamed "Test" clusters; no first-party render cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 (vcpkg buildtrees: SDL_AppIterate, Android/KMSDRM/X11_CreateWindow ...); none first-party |
| 4 | query | "push constants descriptor bindless pipeline layout" | `CreateBindlessTables`, `RegisterBuffer`, `RegisterTexture` (engine/rhi_vulkan/src/VulkanDevice.cpp); the rest spirv-reflect internals |
| 5 | query | "ReflectSpirv shader reflection" | Only spirv-reflect third-party symbols (BM25 index weak, as warned) |
| 6 | query | "ForwardRenderer scene data constant buffer layout upload" | `ForwardRenderer` class (engine/render/include/interi/render/ForwardRenderer.h:103) plus SDL GPU / spirv-cross third-party hits |
| 7 | cypher | engine/ symbols with name containing Reflect/PushConstant/Pipeline/SceneData/Gpu | 69 rows: `ReflectSpirv` (ShaderReflection.cpp:35), `ICommandList::PushConstants` (Device.h:41, template at :70), `kMaxPushConstantSize` (Types.h:160), `Create{Compute,Graphics}Pipeline` (VulkanDevice.cpp:1413/1453), `PipelineData` (VulkanImpl.h:87), `GpuParticleSystem` |
| 8 | context | `ReflectSpirv` (ShaderReflection.cpp) | 3 callers: `CreateComputePipeline`, `CreateGraphicsPipeline`, ComputeTests `TEST`; no outgoing engine calls; result used only for the stage check (led to HLSL-002, HLSL-006) |
| 9 | impact | `ICommandList::PushConstants` upstream, depth 1 | 1 direct (METHOD_OVERRIDES by `VulkanCommandList`), risk LOW; call sites go through the template overload |
| 10 | cypher | CALLS edges into `PushConstants` from engine/ | 0 rows: template-overload call sites are not resolved by the index; confirmed by reading ForwardRenderer.cpp/Particles.cpp |
| 11 | context | `CreateBindlessTables` | 1 caller (`VulkanDevice::Create` candidates lambda), calls `VkError`; defines 4 bindings × `VK_SHADER_STAGE_ALL` and one 128-byte push range |

## Analysis limitations
- HLSL files are not indexed by GitNexus. All shader analysis was done by reading the 26 files directly; the graph was used for the C++ consumers only.
- No DXC run, SPIR-V disassembly or validation-layer run was performed (builds and executables are out of scope). Claims about early-Z behaviour (HLSL-003) and descriptor read-only paths (HLSL-010) are inferred from the shader and pipeline structure and general hardware behaviour, not profiled.
- The BM25 full-text index was unavailable, so `query` results were dominated by third-party symbols. Discovery fell back to `cypher` name filters on `engine/`.
- The template `PushConstants<T>` call sites are not resolved as CALLS edges, so the push-struct inventory in HLSL-002 comes from reading the files.
- `modules/destruction` and `modules/physics` (separate gitignored projects, not indexed) may contain their own shaders or render features; they were not analyzed.

## Cross-discipline notes
- Rendering/Graphics: alpha-blended particles are unsorted (`engine/render/src/Particles.cpp:224-234`), and there is no main-view depth prepass (`engine/render/src/ForwardRenderer.cpp:534-571`). Both are render-architecture choices touched on in HLSL-003 and HLSL-004.
- Build/CMake: `interi_add_shaders` globs every `.hlsli` in each include dir as a dependency of every shader in the target (`cmake/InteriShaders.cmake:23-29`). This is coarse but correct; a depfile (`-MD`/`-MF`) would be finer.
- Testing: there is no test that checks HLSL offsets against C++ `offsetof` (see HLSL-001). A reflection-based unit test could cover push constants (see HLSL-002).
