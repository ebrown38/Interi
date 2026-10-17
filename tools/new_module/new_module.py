# SPDX-License-Identifier: MIT
"""Scaffolds an Interi gameplay Module, Extension or Plugin.

    python3 tools/new_module/new_module.py --kind Module --id com.author.fps --name "FPS" --author "Author" \\
        --output path/to/fps

writes module.json, a standalone CMakeLists.txt (find_package(Interi)), src/<Name>Module.cpp with a stub Register
and a load test (interi_add_module_load_test, run by ctest), ready to build against an installed engine:

    cmake -S path/to/fps -B path/to/fps/build -DCMAKE_PREFIX_PATH=<engine install prefix>
    cmake --build path/to/fps/build && ctest --test-dir path/to/fps/build
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import sys
from pathlib import Path

KINDS = ("Module", "Extension", "Plugin")
MODULE_ID = re.compile(r"^[a-z0-9][a-z0-9_-]*(\.[a-z0-9][a-z0-9_-]*)+$")
DEFAULT_ENGINE_RANGE = "^0.1"


class ScaffoldError(Exception):
    """Invalid arguments or an output directory that would be overwritten."""


def pascal_case(text: str) -> str:
    parts = re.split(r"[^A-Za-z0-9]+", text)
    name = "".join(part[:1].upper() + part[1:] for part in parts if part)
    if not name or not name[0].isalpha():
        name = "Module" + name
    return name


def library_name(module_id: str) -> str:
    return re.sub(r"[^a-z0-9_]", "_", module_id)


def manifest(args: argparse.Namespace, library: str, pascal: str) -> dict:
    year = datetime.date.today().year
    data = {
        "id": args.id,
        "name": args.name,
        "version": "0.1.0",
        "kind": args.kind,
        "author": args.author,
        "license": args.license,
        "copyright": f"Copyright (c) {year} {args.author}",
        "description": args.description or f"{args.name}: an Interi {args.kind.lower()}.",
        "engine": args.engine,
        "library": library,
        "dependencies": [],
    }
    if args.kind == "Extension":
        data["contracts"] = [f"service.{pascal}"]
    if args.kind == "Plugin":
        data["overrides"] = []
    return data


def cmake_lists(args: argparse.Namespace, library: str, pascal: str) -> str:
    return f"""# SPDX-License-Identifier: {args.license}
# {args.name} ({args.id}): an Interi {args.kind}, built against an installed engine.
cmake_minimum_required(VERSION 3.29)
project({library} LANGUAGES CXX)

find_package(Interi REQUIRED)

interi_add_gameplay_module({library} SOURCES src/{pascal}Module.cpp)

# ctest: loads this folder's module.json and the library through the engine's module host.
enable_testing()
interi_add_module_load_test({library})
"""


def module_source(args: argparse.Namespace, pascal: str) -> str:
    header = f"""// SPDX-License-Identifier: {args.license}
// {args.name} ({args.id}): an Interi {args.kind}. Register runs when the engine loads (or reloads) the module; see
// interi/hotreload/ModuleApi.h for the rules and interi/sdk/Hooks.h for hooks.
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

namespace
{{

using interi::sdk::HookNext;
using Example = int(int);

"""
    if args.kind == "Module":
        body = f"""bool Register(interi::hotreload::ModuleRegistrar& registrar)
{{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // A hook that Plugins and higher modules can override ("{args.id}:Example").
    return scope
        .DeclareHook<Example>(scope.Local("Example"), [](const HookNext<Example>&, int value) {{ return value; }})
        .has_value();
}}
"""
    elif args.kind == "Extension":
        body = f"""bool Register(interi::hotreload::ModuleRegistrar& registrar)
{{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // service.{pascal} is a shared contract id this Extension declares (module.json "contracts"); a module above it
    // that provides the same id wins outright.
    return scope
        .DeclareHook<Example>("service.{pascal}", [](const HookNext<Example>&, int value) {{ return value; }})
        .has_value();
}}
"""
    else:
        body = """bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    [[maybe_unused]] const interi::sdk::ModuleScope& scope = registrar.Scope();
    // A Plugin only overrides what other modules expose. Declare the target in module.json, e.g.
    //   "overrides": [{ "target": "com.author.base:Example", "version": "^1" }]
    // then:
    //   return INTERI_OVERRIDE(scope, "com.author.base:Example",
    //                          [](const HookNext<Example>& next, int value) { return next(value) + 1; })
    //       .has_value();
    return true;
}
"""
    return header + body + """
} // namespace

INTERI_GAMEPLAY_MODULE(Register)
"""


def scaffold(args: argparse.Namespace) -> list[Path]:
    if args.kind not in KINDS:
        raise ScaffoldError(f"--kind must be one of {', '.join(KINDS)}")
    if not MODULE_ID.match(args.id):
        raise ScaffoldError(f"invalid module id '{args.id}' (reverse-DNS, lowercase: com.author.name)")
    output = Path(args.output)
    if output.exists() and any(output.iterdir()) and not args.force:
        raise ScaffoldError(f"{output} is not empty (use --force to write into it)")

    pascal = pascal_case(args.name)
    library = library_name(args.id)
    files = {
        output / "module.json": json.dumps(manifest(args, library, pascal), indent=2) + "\n",
        output / "CMakeLists.txt": cmake_lists(args, library, pascal),
        output / "src" / f"{pascal}Module.cpp": module_source(args, pascal),
        output / ".gitignore": "build/\n",
    }
    for path, text in files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    return list(files)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Scaffold an Interi Module, Extension or Plugin.")
    parser.add_argument("--kind", default="Module", choices=KINDS)
    parser.add_argument("--id", required=True, help="reverse-DNS module id, e.g. com.author.fps")
    parser.add_argument("--name", help="display name (default: the id's last label)")
    parser.add_argument("--author", default="Unknown")
    parser.add_argument("--license", default="MIT", help="SPDX license expression")
    parser.add_argument("--description", default="")
    parser.add_argument("--engine", default=DEFAULT_ENGINE_RANGE, help="engine version range")
    parser.add_argument("--output", required=True, help="directory to create")
    parser.add_argument("--force", action="store_true", help="write into a non-empty directory")
    args = parser.parse_args(argv)
    if not args.name:
        args.name = pascal_case(args.id.rsplit(".", 1)[-1])
    return args


def main(argv: list[str]) -> int:
    try:
        written = scaffold(parse_args(argv))
    except ScaffoldError as error:
        print(f"new_module: {error}", file=sys.stderr)
        return 1
    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
