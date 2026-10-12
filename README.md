# Interi

An open-source (MIT) C++ game engine built for one thing: large-scale, multithreaded structural
destruction in the style of *Red Faction: Guerrilla*. Targets PC (Linux/Windows), with console
backends (PS5, Xbox Series X|S) maintained in separate private repositories.

## Building (Linux)

Requirements: Clang (CI and development use Clang 22), lld, CMake 3.29+, Ninja, Python 3, and `clang-tools-extra` for format and tidy.

```sh
git clone --recurse-submodules <repo-url> && cd Interi
external/vcpkg/bootstrap-vcpkg.sh -disableMetrics
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

On Windows, run the same commands with the `windows-msvc-debug` preset from a Visual Studio
Developer prompt (use `bootstrap-vcpkg.bat`).

| Preset | Purpose |
|---|---|
| `linux-clang-debug` / `-release` | Day-to-day development |
| `linux-clang-asan` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `linux-clang-tsan` | ThreadSanitizer (required for anything concurrent) |
| `linux-clang-coverage` | llvm-cov. Run `ctest`, then build the `coverage` target |
| `linux-clang-tidy` | clang-tidy on engine libraries |
| `linux-clang-profile` | Release + [Tracy](https://github.com/wolfpld/tracy) profiling (`INTERI_ZONE` macros) |
| `windows-msvc-debug` / `-release` / `-profile` | MSVC |

The engine targets x86-64-v3 (AVX2/FMA: Haswell, Zen 1 and newer), the same baseline as PS5 and Xbox Series.
Benchmarks live in `benchmarks/`; measure with the release preset, e.g.
`build/linux-clang-release/benchmarks/core/interi_core_benchmarks`.

## Development workflow (TDD)

1. **Red**: write the failing tests first. A test committed before its implementation can be
   tagged with the CTest label `pending`, which every preset and blocking CI job excludes.
2. **Green**: write the smallest implementation that passes.
3. **Refactor**: run the ASan and TSan presets as well.

Every engine module is declared with `interi_add_module(...)` (see `cmake/InteriModule.cmake`).
Configuring a module without `TEST_SOURCES` is an error. Test names follow
`Subject.Condition_ExpectedResult`.

## Licensing

Interi is MIT-licensed. All dependencies must use permissive, MIT-compatible licenses. The
`LicenseCheck_*` tests enforce this against `tools/license_check/license_policy.json`.
Third-party notices are in `THIRD_PARTY_NOTICES.md`. Every source file starts with
`SPDX-License-Identifier: MIT`.
