# Python — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. PY-001, PY-002: fix the compliance tools first. They guard MIT distribution, and fixing both together makes the gate and the notices agree.
2. PY-006: widen SPDX coverage. This is cheap and closes a gap in the same gate family.
3. PY-003, PY-004, PY-010 (the bench_compare part): make the benchmark comparison trustworthy and tested before the destruction gate drops `--report-only`.
4. PY-007: give bad input its documented exit code 2.
5. PY-008, PY-005, PY-009, PY-010 (the encoding part): declare the interpreter, add ruff and mypy to CI, then remove the `sys.path` hacks and the `# noqa` comments.

## PY-001 — A license-policy override replaces any concluded license, not just missing metadata
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `find_violations` (`tools/license_check/check_licenses.py:131-134`), apply an override only when the concluded expression is missing: `None`, empty, `NOASSERTION`, `NONE` or a `LicenseRef-vcpkg-null`-style placeholder.
  2. If vcpkg reports real metadata for a port that has an override, report a violation such as "override is stale: vcpkg now concludes X; re-review". That forces a human to look again after a baseline bump.
  3. Add a test: port `stb` with concluded `GPL-3.0-only` and an override of `MIT` must produce a violation.
- **Illustrative change (not applied):**
  ```python
  MISSING = {None, "", "NOASSERTION", "NONE"}
  if port in overrides:
      if expression in MISSING or expression.startswith("LicenseRef-vcpkg-null"):
          expression = overrides[port]["license"]
      else:
          violations.append(Violation(port, expression, "override is stale: vcpkg now has license metadata"))
          continue
  ```
- **Verification:** The new unit test passes. `ctest -L license` still passes with today's dependency set: stb and dxc have no metadata, so their overrides still apply.
- **Risks / trade-offs:** If vcpkg emits some other placeholder `LicenseRef-*`, it needs to join the "missing" set. Check what the current `vcpkg.spdx.json` files contain for stb and dxc first.

## PY-002 — gen_notices.py ignores the reviewed overrides, so THIRD_PARTY_NOTICES.md shows the wrong stb license
- **Severity / Effort:** Medium / S
- **Depends on:** PY-001 (both should share one "effective license" function)
- **Steps:**
  1. Add a helper in `check_licenses.py`, e.g. `effective_license(port, concluded, policy) -> str | None`, that applies the override rule from PY-001. Use it in both `find_violations` and `generate_notices`.
  2. In `gen_notices.main` (line 55), load the whole policy and pass it to `generate_notices`.
  3. Regenerate `THIRD_PARTY_NOTICES.md` with the documented command, then confirm that stb reads `License: MIT` on Linux and Windows.
  4. Extend `NoticesTests` with an override case.
- **Verification:** `grep -n "LicenseRef-vcpkg-null" THIRD_PARTY_NOTICES.md` returns nothing. `ThirdPartyNotices_UpToDate` passes on both CI platforms.
- **Risks / trade-offs:** Regenerating the notices changes a committed file, which must stay identical on both platforms.

## PY-003 — compare_benchmarks.py passes when benchmarks are missing or the results are empty
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Count baseline benchmarks that are missing from the current run as failures. Allow an explicit `--allow-missing` flag for intentional renames.
  2. Exit 2 if either file has no `benchmarks` key or an empty list.
  3. Print a summary line that separates regressions from missing benchmarks.
- **Illustrative change (not applied):**
  ```python
  missing = sorted(set(baseline) - set(current))
  ...
  failed = regressions + (0 if args.allow_missing else len(missing))
  return 0 if args.report_only or failed == 0 else 1
  ```
- **Verification:** New unit tests: an empty current file exits 2, a current file with one benchmark missing exits 1, and with `--allow-missing` it exits 0.
- **Risks / trade-offs:** Renaming a benchmark then needs a baseline refresh or the flag, which is the intended friction.

## PY-004 — compare_benchmarks.py compares `real_time` without checking `time_unit`
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `_metrics`, convert `real_time` to nanoseconds using `entry.get("time_unit", "ns")` and a fixed table: ns=1, us=1e3, ms=1e6, s=1e9.
  2. Reject an unknown unit with exit 2.
- **Verification:** A unit test where the baseline is `1 ms` and the current run is `1000 us` reports a 0% change.
- **Risks / trade-offs:** None meaningful. The `*_ms` counters are already unit-named.

## PY-005 — The Python tooling has no lint, type-check or format gate
- **Severity / Effort:** Medium / M
- **Depends on:** PY-008 (pick the target version first)
- **Steps:**
  1. Add a minimal `tools/pyproject.toml`, or a root one, with only `[tool.ruff]` (target version matching PY-008; rules `E,F,W,B,UP,SIM,I`; line length 120 to match `.clang-format`), `[tool.ruff.format]` and `[tool.mypy]` (`strict = true`, `files = ["tools"]`).
  2. Annotate the untyped parameters: `is_allowed(expression: str | None, allowed_licenses: Iterable[str], allowed_exceptions: Iterable[str])`, and replace the bare `dict` with `dict[str, Any]` or a `TypedDict` for the policy.
  3. Add a step to the `lint` job in `.github/workflows/ci.yml`: `pip install ruff==<pinned> mypy==<pinned>` (or `dnf install`), then `ruff check tools`, `ruff format --check tools` and `mypy`.
- **Verification:** The CI lint job runs ruff and mypy and fails on a deliberately introduced unused import.
- **Risks / trade-offs:** Adding pinned dev tools to CI brings a small supply-chain surface. Pin exact versions.

## PY-006 — check_spdx_headers.py covers only a hard-coded directory list, and its header match is loose
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Take candidates from `git ls-files` when `--root` is a git work tree. That is a subprocess call: use `check=True`, `text=True`, `encoding="utf-8"`. Otherwise fall back to the directory walk. Alternatively, add `benchmarks` and `samples` to `DEFAULT_DIRS` and remove `game`.
  2. Exit 2 when no candidate files are found, which catches a wrong `--root`.
  3. Match the header exactly with a regex: `SPDX-License-Identifier:\s*MIT\s*$`. Keep in mind that `new_module.py` lets scaffolded out-of-tree modules use other licenses; that doesn't apply inside this repo.
  4. Add tests for `MIT-0` being rejected and for an empty root returning 2.
- **Verification:** `python3 tools/license_check/check_spdx_headers.py` still passes. A temporary file without the header under `samples/` makes it fail.
- **Risks / trade-offs:** The `git ls-files` route needs git in the lint container, which is already installed (`ci.yml` lint job).

## PY-007 — Bad input exits 1, which the docs reserve for violations or regressions; 2 is documented for bad input
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In each `main`, wrap the input loading (`json.loads`, `read_text`, the `KeyError` on required fields) in a function that catches `(OSError, json.JSONDecodeError, KeyError, ValueError)`, prints `error: <file>: <reason>` to stderr and returns 2. `compare_benchmarks._load` already does this for the read.
  2. In `new_module.scaffold`, check `output.exists() and not output.is_dir()` and raise `ScaffoldError`.
  3. Have `check_licenses.main` call `collect_port_licenses` once and pass the result to `find_violations`.
- **Verification:** Tests that pass a malformed SPDX document, a policy without `license` in an override, and a benchmark entry without `real_time` each expect exit 2.
- **Risks / trade-offs:** None.

## PY-008 — The minimum Python version is not declared, but the tests need 3.9 or later
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Change `CMakeLists.txt:49` to `find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)`, or whatever floor the team picks.
  2. Record the same floor as `requires-python` in the pyproject from PY-005 and as ruff's `target-version`.
  3. Add `from __future__ import annotations` to both test files for consistency with the scripts.
- **Verification:** Configuring with an older interpreter fails at configure time with a clear message.
- **Risks / trade-offs:** None. Both CI platforms ship much newer interpreters.

## PY-009 — `sys.path` manipulation instead of a package or `-m` invocation
- **Severity / Effort:** Low / S
- **Depends on:** PY-005 (removing the `# noqa` comments)
- **Steps:**
  1. Either make `tools/license_check` a package (`__init__.py`) and run the tests with `python -m unittest discover -s tools/license_check/tests -t tools`, or keep the scripts flat and set `PYTHONPATH` through the CTest `ENVIRONMENT` property instead of editing `sys.path` in code.
  2. For `gen_notices.py`, which must stay runnable as a script, the `sys.path` line is acceptable if moved into a small `_bootstrap` with a comment. Otherwise switch to `python -m tools.license_check.gen_notices` and update the docs and CMake commands.
- **Verification:** Grepping `tools/` for `sys.path.insert` returns nothing (or only the documented bootstrap). The tests pass from CTest.
- **Risks / trade-offs:** Changing the invocation means updating CLAUDE.md, AGENTS.md and `tools/license_check/CMakeLists.txt`.

## PY-010 — Tests: compare_benchmarks is untested, and the tests use locale-dependent text I/O
- **Severity / Effort:** Low / S
- **Depends on:** PY-003, PY-004
- **Steps:**
  1. Add `tools/bench_compare/tests/test_compare_benchmarks.py` covering: median preference, a regression above the threshold, `--report-only`, missing benchmarks, empty input and unit normalisation.
  2. Add `tools/bench_compare/CMakeLists.txt` with `add_test(NAME BenchCompare_PythonUnitTests ...)` labelled `unit`, plus `add_subdirectory(bench_compare)` in `tools/CMakeLists.txt`.
  3. Pass `encoding="utf-8"` to every `read_text`/`write_text` in both existing test files. Ruff can flag the missing ones once `PLW1514` is enabled.
- **Verification:** `ctest -R BenchCompare_` passes on Linux and Windows CI.
- **Risks / trade-offs:** None.
