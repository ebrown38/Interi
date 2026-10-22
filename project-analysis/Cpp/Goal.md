# Cpp — Goal State

## Vision
Interi's C++ keeps its present strengths: RAII everywhere, deterministic lock-free primitives, `Result`-based I/O, and strict warnings with ASan, UBSan and TSan in CI. It closes three gaps:
- **Type-erased boundaries are self-checking.** Hooks verify their signature on every declaration and call. Component registry entries can never point into an unloaded module.
- **One error policy holds in every configuration.** Runtime failures (allocation, capacity, GPU device loss, hostile files) return `Result` or fail deterministically. The asserts-off shipping build behaves like the tested one, because it is built and tested in CI.
- **Binary readers allocate in proportion to the bytes they consume,** so a malformed or hostile file always ends in `ErrorCode::Corrupt` and never in `bad_alloc` or `std::terminate`.

## Acceptance criteria
- [ ] **AC-1** `HookRegistry::Declare` returns `InvalidArgument` when a hook id already has entries with a different signature. `HookPoint::operator()` never invokes an implementation whose stored signature differs from the handle's. Covered by `Hook.SharedDeclare_SignatureMismatch_Error` and `Hook.Reload_ChangedSignature_OldHandleInvalid` in `tests/sdk/HooksTests.cpp`. (resolves CPP-001)
- [ ] **AC-2** No `INTERI_ASSERT`/`INTERI_VERIFY` is the sole guard before an array index, a dereference or placement-new on a condition caused by input, allocation or capacity. `git grep -n INTERI_VERIFY engine` lists only invariant checks, each reviewed. A death or error test exists for:
  - component registry exhaustion
  - a null chunk or block allocation
  - module component alignment above 64
  - a full bindless table (resolves CPP-002)
- [ ] **AC-3** No `vkQueueSubmit2`, `vkWaitSemaphores` or `vkGetSemaphoreCounterValue` result is discarded or asserted. `IDevice::Submit` and `WaitForValue` report `DeviceLost`/`OutOfMemory` through `Result`, and `lastSubmitted` advances only on a successful submit. A `MockDevice` test and a GPU test with an injected `VK_ERROR_DEVICE_LOST` both pass without abort or hang. (resolves CPP-003)
- [ ] **AC-4** The presets `linux-clang-shipping` and `windows-msvc-shipping` exist, define `INTERI_SHIPPING`, and run `ctest -L unit` green in CI on every push. (resolves CPP-004)
- [ ] **AC-5** `ModuleHost` refuses to load or reload a module that registered a component whose `relocate`/`moveAssign`/`destroy` resolve into the module's image, with a test module variant proving it. No ASan report on unload follows. (resolves CPP-005)
- [ ] **AC-6** `ArchiveReader` never sizes a vector from an unvalidated count: a primitive-element count is rejected when it exceeds `Remaining() / sizeof(ArchiveBits<T>)`, and a non-primitive vector grows only from elements actually parsed, under an explicit element-count and allocation-byte cap (`SetMaxAllocation`). The `std::vector<T>` storage that holds parsed non-primitive elements is not counted against an O(N) bound. A 64-byte crafted archive allocates under 1 KiB and returns `Corrupt`. No `Read*Asset`/`ReadCookedData`/save reader lets `std::bad_alloc` escape. The fuzz harness runs with `-rss_limit_mb=2048` clean. (resolves CPP-006)
- [ ] **AC-7** Every serialized enum declares its range, and `ArchiveReader` fails sticky on an out-of-range value. Validators contain no magic `> N` enum bounds. (resolves CPP-007)
- [ ] **AC-8** Every JSON-to-`float` conversion in `engine/` goes through a helper that rejects `|x| > FLT_MAX`. Feeding `1e39` to each numeric field of `.hud.json`, camera rigs, `.animstates.json`, `.skeleton.json` and `.clip.json` returns a field-named error. (resolves CPP-008)
- [ ] **AC-9** `SmallVector` and `HashMap` buffer allocation throw `std::bad_array_new_length` on size overflow. A throwing element constructor during `SmallVector` growth leaks nothing, as a `TrackedAllocator` test shows. (resolves CPP-009)
- [ ] **AC-10** `.clang-tidy` enables `clang-analyzer-*`, `concurrency-*`, `cppcoreguidelines-pro-type-member-init` and `cppcoreguidelines-missing-std-forward`, and the `linux-clang-tidy` CI job is green with them. (resolves CPP-010)
- [ ] **AC-11** (Info items, no action required) CPP-011 and CPP-012 are either left as documented or closed by the optional notes in Remediation.md. (references CPP-011, CPP-012)

## Guardrails to keep it that way
- CI matrix includes `*-shipping` (asserts off) alongside debug, release, asan (with UBSan), tsan, tidy and profile.
- A review checklist item for every new `INTERI_VERIFY`/`INTERI_ASSERT`: "Can this condition be caused by input, allocation, capacity or the GPU? If yes, return `Result` or use the always-on fatal check."
- New serialized enums must declare their range; a `static_assert` in `ArchiveReader::Process` rejects enums without the trait once all current ones are migrated.
- Hook and component registry behaviour is covered by hot-reload tests that run under ASan and TSan (`ModuleHostTest`, `ModuleProjectTest`); a test module variant exercises each refusal path.
- Fuzz targets cover every binary reader (`.itex`, `.imesh`, `.iskel`, `.ianim`, `.iskin`, `.imat`, `ISAV`, `IVAL`) with an RSS limit.
- The clang-tidy check list only grows; any new `NOLINT` carries a justification comment.
- Keep the existing guards: warnings-as-errors flags in `cmake/InteriWarnings.cmake`, `HeaderCheck.cpp` per module, and the `Component` concept with its `noexcept`-move requirements.

## Out of scope
- Recovering from GPU device loss by recreating the device: reporting it is in scope (AC-3); recovery is not.
- Sandboxing native module code: modules run with full trust by design (the Module Manager's trust prompt).
- Code in `modules/` (physics, destruction), `external/` and console backends in private repos.
- Performance tuning of containers and the job system: they were judged correct, not benchmarked.
- C++20 modules adoption.
