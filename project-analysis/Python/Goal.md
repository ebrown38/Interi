# Python — Goal State

## Vision
Interi's Python stays a small, standard-library-only tooling layer: the license gate, the notices generator, the SPDX header check, the module scaffolder and the benchmark comparator. Each tool is correct in the cases that matter for distribution and performance gating. The gate and the notices share one reviewed definition of a dependency's license. The header check covers every tracked source file. The benchmark comparator never passes a run that lost benchmarks or changed units. Every tool separates "policy failure" (exit 1) from "bad input" (exit 2). The interpreter floor is declared, and ruff, ruff-format and mypy run in CI alongside clang-format.

## Acceptance criteria
- [ ] AC-1 `find_violations` applies a policy override only when vcpkg's concluded license is missing or a placeholder. A unit test where the concluded license is `GPL-3.0-only` and the override is `MIT` reports a violation. (resolves PY-001)
- [ ] AC-2 `gen_notices.py` and `check_licenses.py` take each port's license from one shared function, and `grep -n "LicenseRef-vcpkg-null" THIRD_PARTY_NOTICES.md` returns nothing. `ThirdPartyNotices_UpToDate` passes on Linux and Windows. (resolves PY-002)
- [ ] AC-3 `compare_benchmarks.py` exits 1 when a baseline benchmark is missing (unless `--allow-missing` is given) and exits 2 on an empty or `benchmarks`-less file. Each behaviour has a unit test. (resolves PY-003)
- [ ] AC-4 `compare_benchmarks.py` normalises `real_time` by `time_unit`. A test comparing 1 ms against 1000 us reports a 0% change. (resolves PY-004)
- [ ] AC-5 A committed `pyproject.toml` configures ruff (lint and format) and `mypy --strict` for `tools/`. The CI `lint` job runs `ruff check tools`, `ruff format --check tools` and `mypy`, all with pinned versions, and they pass. (resolves PY-005)
- [ ] AC-6 The SPDX check covers every tracked source and CMake file, including `benchmarks/` and `samples/` (via `git ls-files` or an updated directory list with no `game`). It accepts only an exact `SPDX-License-Identifier: MIT`, and exits 2 when it finds no candidates. (resolves PY-006)
- [ ] AC-7 These cases make the tool exit 2 with a one-line `error:` on stderr and no traceback, each covered by a test: a malformed SPDX or policy JSON file, an override without `license`, a benchmark entry without `real_time`, and `new_module.py --output <existing file>`. (resolves PY-007)
- [ ] AC-8 `find_package(Python3 <floor> ...)` in `CMakeLists.txt` and `requires-python` in `pyproject.toml` declare the same minimum version, and every Python file imports `from __future__ import annotations` or is valid on that floor. (resolves PY-008)
- [ ] AC-9 No `sys.path.insert` remains in `tools/`, except one documented bootstrap if a script must stay directly runnable, and no `# noqa: E402` remains. (resolves PY-009)
- [ ] AC-10 `BenchCompare_PythonUnitTests` is registered in CTest with label `unit` and passes on both CI platforms. Every `read_text`/`write_text` in `tools/**/tests` passes `encoding="utf-8"`. (resolves PY-010)
- [ ] AC-11 (optional) The stray ignored `tools/platform_audit/` bytecode directory is removed from developer checkouts. (PY-011, Info)

## Guardrails to keep it that way
- The CI `lint` job runs ruff check, ruff format check and mypy on `tools/` next to clang-format and the SPDX check.
- Ruff rules `B`, `UP`, `SIM`, `I` and `PLW1514` (unspecified encoding) keep the I/O encoding-safe and the code modern.
- Unit tests pin the semantics of the gates: override staleness, missing benchmarks, unit normalisation, and exit code 2 on bad input.
- `ctest -L license` and `ctest -L unit` stay in CI on both Linux and Windows.
- Preserve the current strengths:
  - standard library only (any new dependency needs pinning and a license-policy entry)
  - no `subprocess` without `check=True` and an explicit encoding
  - `pathlib` everywhere
  - `newline="\n"` on generated files

## Out of scope
- Packaging the tools for PyPI or adding `[project.scripts]` entry points: they are repository-internal and run through CTest and CI.
- vcpkg's own Python scripts under `external/` and anything in the gitignored `modules/` projects (including `modules/destruction/tools/run_benchmarks.sh`).
- Choosing the actual benchmark regression threshold or deciding when the destruction gate leaves `--report-only` (a performance or CI decision).
