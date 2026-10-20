# SPDX-License-Identifier: MIT
"""Run only the tests a change can reach (push and pull_request CI).

`changes` lists the files a GitHub event added, modified or deleted, as `<status>\\t<path>` lines (git's
--name-status, renames split into a delete and an add), or `NONE` for an empty change. A pull request is compared
with its base. A push to another branch is compared with where it left the default branch, so a run cancelled by a
later push loses nothing; a push to the default branch is compared with `before`. It prints `ALL\\t<reason>`
instead when the whole suite must run: schedule and workflow_dispatch, an unknown base, a git failure, or a change
too large to pass through an environment variable.

`select` runs after the build. It reads the Ninja graph (build.ninja, plus `ninja -t deps` so headers count, with
order-only edges as dependencies so the libraries a test loads at run time count, and build-tree paths compiled in
with `-DNAME="<path>"` from compile_commands.json, for the same reason) and the CTest list
(`ctest --show-only=json-v1`), and writes the names of the tests to run, one per line, for `ctest --tests-from-file`.
A test runs when:
  - its executable, a file named on its command line, or a library or tool built under a build directory named on
    its command line is reachable in the graph from a changed file (for the build directory itself, `cmake --install`,
    what the cmake_install.cmake scripts install);
  - a changed file lies under a source path named on its command line (a file stands for its folder, unless that is
    the repository root) or under its source-tree working directory (a `tests` working directory stands for its
    parent: the Python tools' convention);
  - it requires a fixture whose setup test runs (Package_Install -> Package_*).
The whole suite runs when a change touches the build configuration (an input of build.ninja's regeneration, or
FULL_RUN_PATTERNS), deletes a file other than docs, or can't be traced: a file that is compiled into nothing and
isn't a tools/ file a test names. Data files (goldens, fixtures, the starter sample) are read at run time through
paths baked into the tests, which the graph can't see. Docs (DOC_PATTERNS) select nothing.

Usage:
  select_tests.py changes --event <name> [--base <sha>] [--before <sha>] [--branch <name>] [--default-branch <name>]
                    [--ref-type branch|tag]
  select_tests.py select --build-dir build/<preset> --changes-env <variable> --output <file> [--summary <file>]
"""
import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

SOURCE_DIR = Path(__file__).resolve().parents[2]
ALL = "ALL"
NONE = "NONE"
MAX_CHANGES_BYTES = 30000  # Windows caps an environment variable at 32767 characters; beyond this, run everything
FULL_RUN_PATTERNS = [
    ".github/*",
    "CMakePresets.json",
    "vcpkg.json",
    "vcpkg-configuration.json",
    "external/*",
    "cmake/*",
    "*.cmake",
    "CMakeLists.txt",
    "*/CMakeLists.txt",
    "tools/ci/toolchain.env",
]
DOC_PATTERNS = ["*.md", "docs/*", "LICENSE", ".gitignore", ".gitattributes", ".clang-format", ".clang-tidy",
                "*/.clang-tidy"]
NULL_SHA = re.compile(r"^0+$")


# --- changes ----------------------------------------------------------------------------------------------------------

def changed_files(event: str, base: str, before: str, branch: str, default_branch: str, git=None,
                  ref_type: str = "branch") -> list:
    """`<status>\\t<path>` lines for the event, or a single `ALL\\t<reason>` line."""
    git = git or _git
    try:
        if event == "pull_request":
            if not base:
                return [f"{ALL}\tno pull request base"]
            since = base
        elif event == "push" and ref_type != "branch":
            return [f"{ALL}\t{ref_type or 'unknown ref'} push"]
        elif event == "push" and branch != default_branch:
            since = (git("merge-base", f"origin/{default_branch}", "HEAD") or "").strip()
            if not since:
                return [f"{ALL}\tno common commit with {default_branch}"]
        elif event == "push":
            if not before or NULL_SHA.match(before) or git("cat-file", "-e", f"{before}^{{commit}}") is None:
                return [f"{ALL}\tthe previous {default_branch} commit is unknown"]
            since = before
        else:
            return [f"{ALL}\t{event or 'unknown'} event"]
        diff = git("diff", "--name-status", "--no-renames", since, "HEAD")
    except OSError as error:
        return [f"{ALL}\tgit failed: {error}"]
    if diff is None:
        return [f"{ALL}\tgit diff failed"]
    lines = [line for line in diff.splitlines() if line.strip()]
    if len("\n".join(lines).encode("utf-8")) > MAX_CHANGES_BYTES:
        return [f"{ALL}\t{len(lines)} changed files"]
    return lines or [NONE]


def _git(*args):
    """git's stdout, or None when git fails."""
    result = subprocess.run(["git", *args], capture_output=True, text=True, check=False)
    return result.stdout if result.returncode == 0 else None


def parse_changes(text: str):
    """[(status, path)], or (None, reason) when the whole suite must run."""
    changes = []
    for line in text.splitlines():
        if not line.strip():
            continue
        status, _, path = line.partition("\t")
        if status == NONE:
            continue
        if status == ALL:
            return None, path or "requested"
        changes.append((status.strip()[:1], path.strip()))
    return changes, ""


# --- the Ninja graph --------------------------------------------------------------------------------------------------

@dataclass
class Graph:
    dependents: dict = field(default_factory=dict)  # node -> nodes built from it
    compiled: set = field(default_factory=set)  # outputs of compile edges (object files)
    linked: set = field(default_factory=set)  # outputs of link edges (libraries, modules, executables)
    regenerate: set = field(default_factory=set)  # inputs of the edge that rewrites build.ninja

    def add(self, rule: str, outputs: list, inputs: list):
        for node in inputs:
            self.dependents.setdefault(node, set()).update(outputs)
        if "_COMPILER__" in rule:
            self.compiled.update(outputs)
        elif "_LINKER__" in rule:
            self.linked.update(outputs)
        elif rule == "RERUN_CMAKE":
            self.regenerate.update(inputs)

    def reachable(self, start: str) -> set:
        seen, queue = set(), deque([start])
        while queue:
            for node in self.dependents.get(queue.popleft(), ()):
                if node not in seen:
                    seen.add(node)
                    queue.append(node)
        return seen


def normalize(path: str, base: Path) -> str:
    path = path.replace("\\", "/")
    if not os.path.isabs(path):
        path = os.path.join(str(base), path)
    return os.path.normcase(os.path.normpath(path)).replace("\\", "/")  # Windows: lower case, then `/` again


def _logical_lines(text: str):
    """Lines with `$`-newline continuations joined."""
    pending = ""
    for line in text.splitlines():
        if pending:
            line = line.lstrip(" ")
        trailing = len(line) - len(line.rstrip("$"))
        if trailing % 2:
            pending += line[:-1]
            continue
        yield pending + line
        pending = ""
    if pending:
        yield pending


_COLON = object()


def _words(text: str) -> list:
    """Split a build statement on unescaped spaces; an unescaped colon becomes _COLON."""
    words, word, i = [], "", 0
    while i < len(text):
        char = text[i]
        if char == "$" and i + 1 < len(text) and text[i + 1] in " :$":
            word += text[i + 1]
            i += 2
            continue
        if char in " :":
            if word:
                words.append(word)
            word = ""
            if char == ":":
                words.append(_COLON)
        else:
            word += char
        i += 1
    if word:
        words.append(word)
    return words


def parse_ninja(text: str, build_dir: Path, graph: Graph, read=None):
    """Add the build statements of one manifest (and those it includes) to the graph."""
    read = read or (lambda path: path.read_text(encoding="utf-8", errors="replace"))
    for line in _logical_lines(text):
        if line.startswith(("include ", "subninja ")):
            parse_ninja(read(build_dir / line.split(" ", 1)[1].strip()), build_dir, graph, read)
            continue
        if not line.startswith("build "):
            continue
        words = _words(line[len("build "):])
        if _COLON not in words:
            continue
        colon = words.index(_COLON)
        outputs = [normalize(w, build_dir) for w in words[:colon] if w != "|"]
        rest = words[colon + 1:]
        if not rest:
            continue
        inputs = []
        for word in rest[1:]:
            if word == "|@":  # validations are not dependencies
                break
            if word not in ("|", "||"):
                inputs.append(normalize(word, build_dir))
        graph.add(rest[0], outputs, inputs)


def parse_deps(text: str, build_dir: Path, graph: Graph):
    """`ninja -t deps`: each object followed by the indented files its compiler read."""
    target = None
    for line in text.splitlines():
        if not line.strip():
            continue
        if not line[0].isspace():
            target = normalize(line.split(": #deps", 1)[0], build_dir) if ": #deps" in line else None
        elif target:
            graph.dependents.setdefault(normalize(line.strip(), build_dir), set()).add(target)


def parse_compile_commands(text: str, build_dir: Path, graph: Graph):
    """Build-tree paths compiled into objects (`-DNAME="<path>"`): a library a test loads by path depends on its build,
    and a directory on every library or executable linked into it."""
    for entry in json.loads(text):
        obj = normalize(entry.get("output", ""), Path(entry.get("directory", str(build_dir))))
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        for value in re.findall(r'[-/]D\w+=\\?"?([^"\\\s]+)', command):
            path = normalize(value, build_dir)
            if _under(path, normalize(str(build_dir), build_dir)):
                sources = [path] + [out for out in graph.linked if out != path and _under(out, path)]
                for source in sources:
                    graph.dependents.setdefault(source, set()).add(obj)


# --- tests ------------------------------------------------------------------------------------------------------------

@dataclass
class Test:
    name: str
    command: list
    working_dir: str = ""
    fixtures_setup: list = field(default_factory=list)
    fixtures_required: list = field(default_factory=list)


def parse_ctest_json(text: str) -> list:
    tests = []
    for entry in json.loads(text).get("tests", []):
        properties = {p["name"]: p["value"] for p in entry.get("properties", [])}
        as_list = lambda value: value if isinstance(value, list) else [value] if value else []  # noqa: E731
        tests.append(Test(entry["name"], entry.get("command", []), properties.get("WORKING_DIRECTORY", ""),
                          as_list(properties.get("FIXTURES_SETUP")), as_list(properties.get("FIXTURES_REQUIRED"))))
    return tests


def _command_paths(test: Test) -> list:
    """Every path-looking argument: plain arguments, `-DNAME=value` values and `a|b` lists."""
    paths = []
    for arg in test.command[1:]:
        value = arg.split("=", 1)[1] if arg.startswith("-D") and "=" in arg else arg
        # Not os.path.isabs: on Windows (Python 3.13+) a rooted path without a drive is relative.
        paths += [part for part in value.split("|") if re.match(r"^([A-Za-z]:)?[\\/]", part)]
    return paths


def _under(path: str, root: str) -> bool:
    return path == root or path.startswith(root.rstrip("/\\") + "/")


@dataclass
class Selection:
    names: list
    full_reason: str = ""  # non-empty: every test was selected
    untraced: list = field(default_factory=list)


def parse_install_script(text: str, base: Path) -> set:
    """Files and directories a cmake_install.cmake installs (`file(INSTALL ... FILES "<path>" ...)`)."""
    installed = set()
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("file(INSTALL ") and " FILES " in line:
            listed = line.split(" FILES ", 1)[1].split(" FILES_MATCHING", 1)[0]
            installed |= {normalize(path.rstrip("/"), base) for path in re.findall(r'"([^"]+)"', listed)}
    return installed


def select(changes, tests: list, graph: Graph, source_dir: Path, build_dir: Path, installed=frozenset()) -> Selection:
    every = sorted(t.name for t in tests)
    if changes is None:
        return Selection(every, "requested")
    root = normalize(str(source_dir), source_dir)
    build = normalize(str(build_dir), build_dir)
    executables = {normalize(t.command[0], build_dir) for t in tests if t.command}

    source_paths = {t.name: _source_paths(t, root, build) for t in tests}
    affected, chosen, untraced, changed = set(), set(), [], []
    for status, path in changes:
        relative = path.replace("\\", "/")
        if _matches(relative, FULL_RUN_PATTERNS):
            return Selection(every, f"{relative} changes the build configuration")
        node = normalize(relative, source_dir)
        changed.append(node)
        if node in graph.regenerate:
            return Selection(every, f"{relative} is read when CMake configures")
        doc = _matches(relative, DOC_PATTERNS)
        if status == "D" and not doc:
            return Selection(every, f"{relative} was deleted")
        reached = graph.reachable(node)
        affected |= reached
        explained = doc or bool(reached & graph.compiled)
        for test in tests:
            for source_path in source_paths[test.name]:
                if source_path == root:  # a whole-repository scan (SPDX headers, platform audit)
                    chosen.add(test.name)
                elif _under(node, source_path):
                    chosen.add(test.name)
                    explained = explained or _under(node, normalize("tools", source_dir))
        if not explained:
            untraced.append(relative)
    if untraced:
        return Selection(every, f"{untraced[0]} is not compiled into anything a test can be traced to", untraced)

    built = affected & graph.linked - executables
    install_changed = any(_under(node, path) for path in installed for node in changed + sorted(affected))
    for test in tests:
        if not test.command:
            continue
        if normalize(test.command[0], build_dir) in affected:
            chosen.add(test.name)
            continue
        for arg in _command_paths(test):
            arg = normalize(arg, build_dir)
            if arg == build:
                hit = install_changed
            else:
                hit = arg in affected or (_under(arg, build) and any(_under(out, arg) for out in built))
            if hit:
                chosen.add(test.name)
                break

    setups = {f for t in tests if t.name in chosen for f in t.fixtures_setup}
    chosen |= {t.name for t in tests if setups & set(t.fixtures_required)}
    return Selection(sorted(chosen))


def _source_paths(test: Test, root: str, build: str) -> list:
    paths = []
    for path in (normalize(p, Path(root)) for p in _command_paths(test)):
        paths.append(path)
        if not os.path.isdir(path) and os.path.dirname(path) != root:
            paths.append(os.path.dirname(path))  # the files next to a script (license_policy.json, templates)
    if test.working_dir:
        working = normalize(test.working_dir, Path(root))
        paths += [working, os.path.dirname(working)] if os.path.basename(working) == "tests" else [working]
    return [p for p in paths if _under(p, root) and not _under(p, build)]


def _matches(path: str, patterns: list) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


# --- command line -----------------------------------------------------------------------------------------------------

def _cache_value(build_dir: Path, key: str) -> str:
    for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    raise KeyError(f"{key} not in {build_dir / 'CMakeCache.txt'}")


def _run(command: list, cwd: Path) -> str:
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=True).stdout


def summary(selection: Selection, total: int) -> str:
    if selection.full_reason:
        return f"Selected all {total} tests: {selection.full_reason}.\n"
    lines = [f"Selected {len(selection.names)} of {total} tests (those the change can reach)."]
    if 0 < len(selection.names) <= 50:
        lines += [""] + [f"- `{name}`" for name in selection.names]
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    changes = commands.add_parser("changes")
    changes.add_argument("--event", required=True)
    changes.add_argument("--base", default="")
    changes.add_argument("--before", default="")
    changes.add_argument("--branch", default="", help="the pushed branch")
    changes.add_argument("--default-branch", default="main")
    changes.add_argument("--ref-type", default="branch", help="github.ref_type: a tag push runs everything")
    chooser = commands.add_parser("select")
    chooser.add_argument("--build-dir", type=Path, required=True)
    chooser.add_argument("--changes-env", required=True, help="environment variable holding the `changes` output")
    chooser.add_argument("--output", type=Path, required=True)
    chooser.add_argument("--summary", type=Path)
    args = parser.parse_args(argv)

    if args.command == "changes":
        print("\n".join(changed_files(args.event, args.base, args.before, args.branch, args.default_branch,
                                       ref_type=args.ref_type)))
        return 0

    build_dir = args.build_dir.resolve()
    graph = Graph()
    parse_ninja((build_dir / "build.ninja").read_text(encoding="utf-8", errors="replace"), build_dir, graph)
    parse_deps(_run([_cache_value(build_dir, "CMAKE_MAKE_PROGRAM"), "-t", "deps"], build_dir), build_dir, graph)
    if (build_dir / "compile_commands.json").exists():
        parse_compile_commands((build_dir / "compile_commands.json").read_text(encoding="utf-8"), build_dir, graph)
    installed = set()
    for directory, subdirectories, files in os.walk(build_dir):
        subdirectories[:] = [d for d in subdirectories if d not in ("vcpkg_installed", "package-test", "CMakeFiles")]
        if "cmake_install.cmake" in files:
            script = Path(directory) / "cmake_install.cmake"
            installed |= parse_install_script(script.read_text(encoding="utf-8", errors="replace"), build_dir)
    tests = parse_ctest_json(_run([_cache_value(build_dir, "CMAKE_CTEST_COMMAND"), "--show-only=json-v1"], build_dir))
    text = os.environ.get(args.changes_env, "")
    parsed, reason = parse_changes(text) if text.strip() else (None, f"${args.changes_env} is empty")
    selection = select(parsed, tests, graph, SOURCE_DIR, build_dir, installed)
    if parsed is None:
        selection.full_reason = reason
    args.output.write_text("".join(f"{name}\n" for name in selection.names), encoding="utf-8")
    report = summary(selection, len(tests))
    print(report, end="")
    if args.summary:
        with args.summary.open("a", encoding="utf-8") as out:
            out.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
