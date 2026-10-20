# Python — Issues

**Project:** Interi (`/home/eric/CLionProjects/Interi`)
**Analyzed:** 2026-10-19
**Scope:** The 7 tracked first-party Python files under `tools/`: `bench_compare/compare_benchmarks.py`, `license_check/{check_licenses,check_spdx_headers,gen_notices}.py`, `license_check/tests/test_license_tools.py`, `new_module/new_module.py` and `new_module/tests/test_new_module.py`. Also how they are wired in (`tools/CMakeLists.txt`, `tools/license_check/CMakeLists.txt`, `tools/new_module/CMakeLists.txt`, root `CMakeLists.txt:49`, `.github/workflows/ci.yml`), plus `tools/license_check/license_policy.json` and the generated `THIRD_PARTY_NOTICES.md`. Excluded: the vcpkg Python under `external/`, build trees and the gitignored `modules/`.
**Summary:** 11 issues — Critical: 0 · High: 0 · Medium: 5 · Low: 5 · Info: 1

**Checked and found clean (keep it this way):**
- The tools use only the standard library, so there are no third-party dependencies to pin.
- There is no `subprocess`, `os.system`, `eval` or `pickle`.
- All path handling uses `pathlib`.
- Production I/O passes `encoding="utf-8"`, and generated files are written with `newline="\n"`, so output is byte-identical on Windows and Linux.
- Every script has an `argparse` CLI with documented exit codes and `sys.exit(main())`, and returns non-zero on its normal failure paths.
- There are no bare `except:` clauses, no mutable default arguments and no `import *`.
- `__pycache__/` is gitignored.

## Findings

### PY-001 — A license-policy override replaces any concluded license, not just missing metadata
- **Severity:** Medium
- **Confidence:** High
- **Category:** Correctness (compliance gate)
- **Location:** `tools/license_check/check_licenses.py:131-134`, `tools/license_check/license_policy.json` (`overrides`), `tools/license_check/tests/test_license_tools.py:94-98`
- **Evidence:**
  ```python
  for port, expression in collect_port_licenses(installed_dir).items():
      if port in overrides:
          # Overrides only fill in missing metadata after manual review; they are still checked.
          expression = overrides[port]["license"]
  ```
  The comment and CLAUDE.md ("A port without license metadata (`stb`) gets a reviewed `overrides` entry") both say an override applies only when metadata is missing. The code replaces the expression whatever vcpkg concluded. The test named `test_override_cannot_launder_disallowed_license` only covers an override that is itself disallowed. It never covers the case where vcpkg reports a real license and the override hides it.
- **Impact:** `stb` and `directx-dxc` have overrides today. If a vcpkg baseline bump gives one of them real license metadata that is not on the allowlist (for example, a relicense), `LicenseCheck_AllDepsOnAllowlist_Passes` still passes on the stale reviewed value. That is a silent pass in the gate that protects MIT distribution. Today's dependency set does not trigger it.

### PY-002 — gen_notices.py ignores the reviewed overrides, so THIRD_PARTY_NOTICES.md shows the wrong stb license
- **Severity:** Medium
- **Confidence:** High
- **Category:** Correctness (generated artefact)
- **Location:** `tools/license_check/gen_notices.py:44`, `tools/license_check/gen_notices.py:55`, `THIRD_PARTY_NOTICES.md:18793`
- **Evidence:** `main` reads the policy only for `build_tools` (`json.loads(...).get("build_tools", {})`, line 55). `generate_notices` prints the raw `licenseConcluded` (`License: {license_expr or 'NOASSERTION'}`, line 44). The committed notices therefore say `## stb` … `License: LicenseRef-vcpkg-null`, but the policy records that Interi takes stb under MIT.
- **Impact:** The shipped attribution file states an unhelpful license for a dependency. It also disagrees with what the license gate actually checked. The two tools read the same policy in different ways.

### PY-003 — compare_benchmarks.py passes when benchmarks are missing or the results are empty
- **Severity:** Medium
- **Confidence:** High
- **Category:** Error handling / exit codes
- **Location:** `tools/bench_compare/compare_benchmarks.py:25`, `tools/bench_compare/compare_benchmarks.py:62-63`, `tools/bench_compare/compare_benchmarks.py:79`
- **Evidence:**
  - Baseline entries that are absent from the current run are only printed as `missing` (lines 62-63) and never counted in `regressions`.
  - `data.get("benchmarks", [])` (line 25) quietly accepts a JSON document that has no `benchmarks` key.
  - As a result, a crashed, filtered or truncated benchmark run produces "0 regression(s)" and exit 0 (line 79).
- **Impact:** Today the only consumer, `modules/destruction/tools/run_benchmarks.sh:17`, passes `--report-only`, which hides this. As soon as the gate is enforced, a benchmark that stops running looks like a pass. The tool has no tests and no CTest registration (see PY-010).

### PY-004 — compare_benchmarks.py compares `real_time` without checking `time_unit`
- **Severity:** Medium
- **Confidence:** High
- **Category:** Correctness
- **Location:** `tools/bench_compare/compare_benchmarks.py:36-41`, `tools/bench_compare/compare_benchmarks.py:57`
- **Evidence:** `_metrics` takes `float(entry["real_time"])` as a raw number. Google Benchmark writes `real_time` in each entry's `time_unit` (ns/us/ms/s). The ratio on line 57 compares the two raw numbers without checking or normalising the units.
- **Impact:** If a benchmark's `->Unit(...)` changes between the baseline and the current run, the result is off by 1000x. It shows as either a large false regression or a hidden real one.

### PY-005 — The Python tooling has no lint, type-check or format gate
- **Severity:** Medium
- **Confidence:** High
- **Category:** Typing and static analysis
- **Location:**
  - The repo root has no `pyproject.toml`, `ruff.toml`, `setup.cfg`, `mypy.ini` or `.pre-commit-config.yaml`.
  - `.github/workflows/ci.yml` lint job (clang-format and SPDX only).
  - `tools/license_check/check_licenses.py:97` (`is_allowed(expression, allowed_licenses, allowed_exceptions)` has untyped parameters).
  - `tools/license_check/gen_notices.py:17` and `tools/license_check/tests/test_license_tools.py:11-13` (`# noqa: E402`).
- **Evidence:**
  - Annotations are mostly present but inconsistent: bare `dict` in `policy: dict` and `dict[str, dict]`, and untyped parameters in `is_allowed` and in the test helpers.
  - The `# noqa` comments show that someone lints by hand, but no linter is configured or run.
  - CI checks C++ formatting with `--Werror` but never checks Python.
- **Impact:** Typing and lint regressions in the compliance and scaffolding tools are only caught at runtime, on the CTest path. The C++ side has a much stronger standard (warnings as errors, clang-tidy).

### PY-006 — check_spdx_headers.py covers only a hard-coded directory list, and its header match is loose
- **Severity:** Low
- **Confidence:** High
- **Category:** Correctness (compliance gate coverage)
- **Location:** `tools/license_check/check_spdx_headers.py:14`, `tools/license_check/check_spdx_headers.py:26`, `tools/license_check/check_spdx_headers.py:31-40`, `tools/license_check/check_spdx_headers.py:51`
- **Evidence:**
  - `DEFAULT_DIRS = ["cmake", "engine", "testing", "tests", "tools", "game"]` leaves out `benchmarks/` (14 tracked files) and `samples/` (the committed starter module, which has C++ and CMake). It also lists `game`, which doesn't exist.
  - Candidates come from `rglob` over the filesystem, not `git ls-files`, so locally the check also reads ignored and untracked files.
  - A wrong `--root` finds no candidates and exits 0.
  - `HEADER in line` is a substring test, so `SPDX-License-Identifier: MIT-0` and `SPDX-License-Identifier: MIT OR GPL-3.0-only` are both accepted.
  - All tracked files in `benchmarks/` and `samples/` carry the header today (checked with `head -5`).
- **Impact:** A new top-level source folder, or a file in `benchmarks/` or `samples/`, can ship without the MIT header without CI noticing. CLAUDE.md requires the header on every source and CMake file.

### PY-007 — Bad input exits 1, which the docs reserve for violations or regressions; 2 is documented for bad input
- **Severity:** Low
- **Confidence:** High
- **Category:** Error handling / exit codes
- **Location:** `tools/license_check/check_licenses.py:111`, `tools/license_check/check_licenses.py:134`, `tools/license_check/check_licenses.py:148`, `tools/license_check/gen_notices.py:55`, `tools/bench_compare/compare_benchmarks.py:26`, `tools/bench_compare/compare_benchmarks.py:37`, `tools/new_module/new_module.py:191`
- **Evidence:** Each docstring says that exit 2 means usage or input error. These cases raise uncaught instead, and Python exits 1 with a traceback:
  - a malformed `vcpkg.spdx.json` or policy file (`json.loads`)
  - an override without a `"license"` key (`KeyError`)
  - a benchmark entry without `name` or `real_time`
  - in `new_module.py`, `--output` pointing at an existing file (`output.iterdir()` raises `NotADirectoryError`)

  `main` in check_licenses also parses every SPDX document twice (lines 149 and 154, through `find_violations`).
- **Impact:** CTest still fails closed, so this is never a false pass. A broken input does look like a policy violation or a regression, though, which misleads whoever triages the CI failure.

### PY-008 — The minimum Python version is not declared, but the tests need 3.9 or later
- **Severity:** Low
- **Confidence:** High
- **Category:** Packaging and reproducibility
- **Location:** `CMakeLists.txt:49`, `tools/new_module/tests/test_new_module.py:16`
- **Evidence:**
  - `find_package(Python3 REQUIRED COMPONENTS Interpreter)` sets no version.
  - `test_new_module.py` has no `from __future__ import annotations`, so `-> tuple[int, Path]` is evaluated when the module is imported, which needs Python 3.9 or later.
  - The production scripts use `str | None` safely behind the future import.
  - There is no `requires-python` anywhere. The local interpreter is 3.14.7, and the CI images are Fedora 44 and the GitHub Windows runner default.
- **Impact:** On an older interpreter, configure succeeds and the failure shows up later as a confusing `TypeError` during test collection.

### PY-009 — `sys.path` manipulation instead of a package or `-m` invocation
- **Severity:** Low
- **Confidence:** High
- **Category:** Packaging / idioms
- **Location:** `tools/license_check/gen_notices.py:16`, `tools/license_check/tests/test_license_tools.py:9`, `tools/new_module/tests/test_new_module.py:10`
- **Evidence:** `sys.path.insert(0, str(Path(__file__).resolve().parent))` followed by `from check_licenses import ...  # noqa: E402`. The tests insert their parent directory the same way. CTest runs them with `WORKING_DIRECTORY .../tests`.
- **Impact:** Imports depend on where the file sits on disk, and the import-order lint rule has to be suppressed. This is minor while the tooling is this small.

### PY-010 — Tests: compare_benchmarks is untested, and the tests use locale-dependent text I/O
- **Severity:** Low
- **Confidence:** High
- **Category:** Tests / cross-platform I/O
- **Location:** `tools/bench_compare/compare_benchmarks.py` (no test and not in `tools/CMakeLists.txt`), `tools/license_check/tests/test_license_tools.py:28-29`, `tools/license_check/tests/test_license_tools.py:130`, `tools/new_module/tests/test_new_module.py:30`, `tools/new_module/tests/test_new_module.py:36`
- **Evidence:**
  - `tools/CMakeLists.txt` adds `license_check`, `module_check`, `new_module` and `editor`, but not `bench_compare`. Its only caller is outside this repo (`modules/destruction/tools/run_benchmarks.sh:17`).
  - The tests call `write_text(...)` and `read_text()` without `encoding=`, while `new_module.py` writes UTF-8 explicitly. These tests run in CTest on Windows too, where the default codec is the ANSI code page.
- **Impact:** The behaviour in PY-003 and PY-004 can change without anyone noticing. A future non-ASCII fixture, such as an author name, would fail only on Windows.

### PY-011 — An ignored, untracked `tools/platform_audit/` directory contains only `__pycache__`
- **Severity:** Info
- **Confidence:** High
- **Category:** Repository hygiene
- **Location:** `tools/platform_audit/__pycache__/platform_audit.cpython-314.pyc`, `tools/platform_audit/tests/__pycache__/`
- **Evidence:** `git status --ignored` shows `!! tools/platform_audit/`. The directory has no sources, only bytecode from a removed or never-committed tool.
- **Impact:** None in CI. Locally, `check_spdx_headers.py` walks it (see PY-006), and it could confuse a reader. Deleting it is optional.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 614 processes, indexed @ 4631f35 = HEAD (fresh) |
| 2 | cypher | Community by symbolCount LIMIT 30 | Top 30 clusters are all third-party (SDL Joystick/Hidapi/Video, Tracy, Manifold, ImGui docking); no first-party Python cluster |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 buildtree flows (SDL_AppIterate/Android_CreateWindow…); no Python processes |
| 4 | cypher | DEFINES from `tools/*.py` files | 49 symbols across the 7 first-party Python files (main/compare/_load, is_allowed/collect_ports/find_violations, scaffold/parse_args, test classes) |
| 5 | query | "subprocess run cmake build tool script" | gen_notices.generate_notices/main; rest are vcpkg/SDL Python; no first-party subprocess use |
| 6 | query | "license check SPDX notices generation" | NoticesTests, gen_notices.HEADER/generate_notices/main, check_licenses.is_allowed/collect_port_licenses/main, new_module.manifest |
| 7 | query | "benchmark compare regression threshold json" | compare_benchmarks.compare/main; remainder are vendored google-benchmark tools |
| 8 | context | `collect_ports` (check_licenses.py) | 2 callers: collect_port_licenses, gen_notices.generate_notices: shared reader for gate and notices |
| 9 | impact | `scaffold` (new_module.py), upstream, with tests | LOW risk; d1 = main; d2/d3 = 9 NewModuleTests methods; 0 processes |
| 10 | cypher | IMPORTS between `tools/*.py` files | 5 edges: gen_notices→check_licenses; tests→their modules; no cycles, compare_benchmarks imported by nothing |

## Analysis limitations
- Static reading only: no tests were run, and neither `ruff` nor `mypy` was executed (both forbidden or not installed).
- The `query` BM25 index failed to build, so the queries ran on vectors and mostly returned vendored vcpkg Python. First-party symbols were enumerated with `cypher` instead.
- The Windows behaviour of the tools (code page, path case) was inferred from the code and not executed on Windows.
- `modules/` is out of scope and not indexed. Only the one line in `modules/destruction/tools/run_benchmarks.sh` that calls `compare_benchmarks.py` was looked at.

## Cross-discipline notes
- Open Source / Licensing: `THIRD_PARTY_NOTICES.md:18793` lists stb as `LicenseRef-vcpkg-null`, but the policy concludes MIT (PY-002). Override semantics are covered in PY-001.
- CI/Build: the CI lint job (`.github/workflows/ci.yml`, job `lint`) has no Python step; `bench_compare` has no CTest wiring.
- Unit-Tests: `tools/bench_compare/compare_benchmarks.py` has no tests at all.
