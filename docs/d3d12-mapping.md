# Mapping the RHI to Direct3D 12

Notes for writing a D3D12 backend (Xbox Series, or a Windows D3D12 path) as an external platform project
(`-DINTERI_EXTERNAL_PLATFORM_DIR=<dir>`, `cmake/InteriExternalPlatform.cmake`). D3D12 headers never enter this
repository: `tools/platform_audit` fails the build when a `d3d12*.h`, `dxgi*.h` or console SDK header appears here.
The backend implements `interi::rhi::IDevice`, `ICommandList` and `ISwapchain` (`engine/rhi`), as `rhi_vulkan` does.

## Model

| RHI | Vulkan (`rhi_vulkan`) | D3D12 |
| --- | --- | --- |
| Timeline (`Submit` signals the next value, `CompletedValue`, `WaitForValue`) | timeline semaphore | `ID3D12Fence` + `SetEventOnCompletion`; `Submit` = `ExecuteCommandLists` then `Signal` |
| Single queue | one graphics+compute queue | one direct queue (it also runs compute) |
| `CommandPoolHandle` / `BeginCommandList` | `VkCommandPool` / primary buffer | `ID3D12CommandAllocator` / `ID3D12GraphicsCommandList`; `ResetCommandPool` = `allocator->Reset()` once the fence passed |
| Bindless: one descriptor set, 128 bytes of push constants | descriptor indexing, `VkPushConstants` | one shader-visible SRV/UAV heap and one sampler heap bound for the whole frame; a root signature of 32 root constants (128 bytes) plus the two heap tables |
| `BindlessIndex(buffer)` (raw `RWByteAddressBuffer`, binding 0) | storage buffer array | UAV in the heap, `ByteAddressBuffer` view (`D3D12_BUFFER_UAV_FLAG_RAW`) |
| `BindlessIndex(texture)` (`Texture2D` / `Texture2DArray`, binding 1) | sampled image array | SRV in the heap |
| `BindlessStorageIndex(texture, mip)` (R32Float, binding 3) | storage image array | one UAV per mip |
| `BindlessIndex(sampler)` (binding 2) | sampler array | the sampler heap |
| Vertex pulling (no input layout) | buffer device address | the index is passed as a root constant; vertices come from raw buffer loads, so the pipeline has an empty input layout |
| `Barriers(buffers, textures)` (sync2-style) | `vkCmdPipelineBarrier2` | enhanced barriers (`ID3D12GraphicsCommandList7::Barrier`) where available, else `ResourceBarrier` transitions + UAV barriers |
| `AliasingBarrier` | memory barrier on the aliased heap | `D3D12_RESOURCE_BARRIER_TYPE_ALIASING` |
| `CreateHeap`, `CreatePlacedTexture/Buffer` | VMA pools / placed binds | `ID3D12Heap`, `CreatePlacedResource` (heap tier 2 for mixed buffers and textures) |
| `DrawIndexedIndirectCount` | `vkCmdDrawIndexedIndirectCount` | `ExecuteIndirect` with a command signature (draw-indexed arguments, one root constant for the object index) and the count buffer |
| `BeginRendering` / `EndRendering` | dynamic rendering | `OMSetRenderTargets` (or render passes, `BeginRenderPass`) |
| `CopyBufferToTexture` / `CopyTextureToBuffer` (row pitch 256) | `vkCmdCopyBufferToImage` | `CopyTextureRegion` with placed footprints; the 256-byte row pitch is exactly `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` |

## Conventions that already match

- Clip space is +Y up with depth 0..1, counter-clockwise front faces, reverse-Z (clear 0, compare `Greater`): D3D12's
  native convention, so the Vulkan backend's viewport flip is simply not applied.
- Shaders are HLSL compiled by DXC. The SPIR-V path uses `[[vk::...]]` attributes (binding numbers, push constants,
  `BaseInstance`); the D3D12 build compiles the same sources to DXIL (SM 6.6) with the root signature declared by the
  backend, so bindless indexing uses `ResourceDescriptorHeap[...]` / `SamplerDescriptorHeap[...]`.
  `engine/shaders/include/interi/Bindless.hlsli` is the single place the tables are declared; add a `#ifdef` there
  for DXIL rather than editing shaders.
- `HLSL discard` compiles to demote-to-helper on SM 6.6; D3D12 needs no extra feature bit.

## Things to decide in the backend

- Descriptor lifetime follows `ResourceManager`'s deferred destroy: a slot is recycled at `LastSubmittedValue() + 1`
  just as a Vulkan index is.
- The Vulkan backend reflects SPIR-V (`ReflectSpirv`) and rejects resources outside the four bindless tables. The
  D3D12 backend should do the same with DXC reflection so a shader can't use a root parameter the backend doesn't have.
- Swapchain: `ISwapchain` maps to a flip-model `IDXGISwapChain3/4`; the surface comes from the platform backend as an
  opaque handle exactly like `VkSurfaceKHR` does (the RHI never learns an `HWND`).
- Validation: enable the D3D12 debug layer + GPU-based validation in the backend's test fixture, as the Vulkan tests
  turn on validation and synchronization validation, and fail a test on any message.
