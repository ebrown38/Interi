# HLSL — Goal State

## Vision
Every shader is compiled offline by DXC at SM 6.6 with warnings as errors, and has a debuggable variant for development builds. The CPU/GPU contract cannot drift silently: scene, particle and decal layouts are defined once or checked field by field at compile time; push-constant sizes and descriptor types are checked against SPIR-V reflection when a pipeline is created and when constants are pushed. The forward pass keeps early depth rejection for opaque geometry, and per-frame GPU cost scales with live work (live particles, lights), not with capacity. The shaders stay free of undefined behaviour (divergent bindless indices are marked, barriers sit in uniform flow, there are no data races or NaN sources on degenerate input) and wave-size agnostic. The source is ready to be compiled for a D3D12/console backend through a thin platform macro layer.

## Acceptance criteria
- [ ] AC-1 Every field the HLSL reads from `FrameData`, `ObjectData`, the particle `FrameHeader`/`Particle` and `DecalHeader`/`DecalData` is checked on both sides: an `offsetof` `static_assert` on the C++ struct, plus HLSL field-order validation (the fields come from a shared C++/HLSL definition, or a build step checks the HLSL struct's member order and offsets against the C++ ones, e.g. from DXC/SPIR-V reflection or a generated check). Swapping two same-sized fields in either the C++ or the HLSL declaration fails the build. `kMaxShadowCascades != 4` fails the build until the HLSL is updated. (resolves HLSL-001)
- [ ] AC-2 Each C++ push-constant struct has a `static_assert(sizeof)` matching its HLSL struct. `PipelineData` keeps the reflected push size and workgroup size, and an assert-enabled build fails a test that pushes fewer bytes than the bound pipeline needs. (resolves HLSL-002)
- [ ] AC-3 `ReflectSpirv` rejects a resource whose descriptor type doesn't match its bindless binding, and a `tests/rhi_vulkan` test with a `cbuffer` shader proves it. (resolves HLSL-006)
- [ ] AC-4 The main opaque forward pipeline has no `discard` (the dithered objects use a separate pipeline and indirect list). A GPU capture shows early-Z active for the opaque forward pass, and the golden images are unchanged. (resolves HLSL-003)
- [ ] AC-5 With 0 live particles, the particle draw pass issues 0 vertex invocations. Draw counts come from a compacted indirect list written by the simulate dispatch. (resolves HLSL-004)
- [ ] AC-6 `GpuParticleSystem::Create` returns an error for `capacity == 0` or `maxEmitPerFrame > capacity`, and a unit test covers it. (resolves HLSL-005)
- [ ] AC-7 Debug presets compile shaders with `-Od -Zi -fspv-debug=vulkan-with-source` (or an equivalent option). Release SPIR-V is unchanged. (resolves HLSL-007)
- [ ] AC-8 A GPU test with a light at the shaded point, a zero-radius light and a zero-length normal reads back no NaN or Inf pixels. (resolves HLSL-008)
- [ ] AC-9 ImGui and particle sRGB colours are decoded with the exact piecewise sRGB curve from a shared include. An 8-bit colour round-trips through an sRGB target within ±1. (resolves HLSL-009)
- [ ] AC-10 Read-only buffer loads use a `ByteAddressBuffer` alias carrying `NonWritable` in SPIR-V, and writable access is limited to the shaders that store or use atomics. (resolves HLSL-010)
- [ ] AC-11 Light culling transforms each light into view space at most once per thread group (or once per frame), and per-cluster overflow is counted and observable. The GPU/CPU clustering tests still agree. (resolves HLSL-011)
- [ ] AC-12 Push constants, bindless declarations and the base-instance input go through a platform include. Every shader also compiles to DXIL with DXC `-WX` in a CI check, and the SPIR-V output is byte-identical to before. (resolves HLSL-012)
- [ ] AC-13 Every bindless index that can differ within a draw or dispatch (any index loaded from per-object or per-particle data) is wrapped in `NonUniformResourceIndex`. This is checked in review. (preserves HLSL-013)
- [ ] AC-14 Any GPU computation that must match a CPU reference bit for bit uses `precise`, or the test documents its tolerance. (preserves HLSL-014)

## Guardrails to keep it that way
- Keep `-WX` and `-HV 2021` on the DXC command line in `cmake/InteriShaders.cmake`. Don't add an FXC path.
- `static_assert` blocks next to every CPU mirror struct, with a comment naming the `.hlsli` file. Add a CLAUDE.md note: "add an offset assert when you add a field the HLSL reads".
- Reflection-based checks at pipeline creation (descriptor types, set/binding range, push size) and at `PushConstants` (size against the bound pipeline) in assert builds.
- Vulkan validation and synchronization validation stay on for every GPU test (already the case). Any validation error fails the test.
- Golden-image tests on lavapipe for the forward, particle and decal passes. Regenerate them only with `INTERI_UPDATE_GOLDEN=1` and after visual inspection.
- Review checklist for shader PRs:
  - `NonUniformResourceIndex` on divergent indices.
  - `GroupMemoryBarrierWithGroupSync` only in uniform control flow.
  - No wave intrinsics that assume a wave size (use `WaveGetLaneCount()` or `[WaveSize]`).
  - Guard `normalize`/`rsqrt`/division against zero.
  - No `discard` in the opaque depth-writing pipeline.
- Optional CI step: compile all shaders to DXIL as well (AC-12), so the console and D3D12 path doesn't rot.

## Out of scope
- Rendering-architecture redesign (deferred shading, visibility buffer, OIT) beyond the specific passes named above.
- Shaders inside `modules/destruction` and `modules/physics` (separate projects, not analyzed).
- Console-specific shader compilers and their platform extensions (private repositories).
- Bit-exact CPU/GPU parity for visual-only systems (particles), which is documented as tolerance-based.
