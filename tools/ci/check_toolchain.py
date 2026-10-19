# SPDX-License-Identifier: MIT
"""Checks that CI's compilers are the versions pinned in toolchain.env.

  check_toolchain.py clang clang-format msvc   print each version, fail when its major.minor differs from the pin
  check_toolchain.py --get MSVC_TOOLSET        print one pinned value (fed to ilammy/msvc-dev-cmd's `toolset`)

With GITHUB_OUTPUT set, each checked tool's version is also written as an output (`clang=22.1`) for cache keys.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_PINS = Path(__file__).resolve().parent / "toolchain.env"

# tool -> (pin key, command, banner pattern capturing major.minor)
TOOLS = {
    "clang": ("CLANG", ["clang", "--version"], r"clang version (\d+\.\d+)"),
    "clang-format": ("CLANG_FORMAT", ["clang-format", "--version"], r"clang-format version (\d+\.\d+)"),
    # cl prints its banner on stderr; toolset 14.xx is compiler 19.xx.
    "msvc": ("MSVC_TOOLSET", ["cl"], r"Compiler Version (\d+\.\d+)"),
}


def load_pins(path: Path):
    pins = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition("=")
        if not sep or not re.fullmatch(r"\d+\.\d+", value.strip()):
            raise ValueError(f"{path}: expected KEY=major.minor, got {line!r}")
        pins[key.strip()] = value.strip()
    return pins


def expected_version(tool: str, pins) -> str:
    """The major.minor the tool's banner must report."""
    pin = pins[TOOLS[tool][0]]
    if tool == "msvc":
        major, minor = pin.split(".")
        if major != "14":
            raise ValueError(f"MSVC_TOOLSET must be 14.xx, got {pin}")
        return f"19.{minor}"
    return pin


def parse_version(tool: str, banner: str):
    match = re.search(TOOLS[tool][2], banner)
    return match.group(1) if match else None


def run_banner(tool: str) -> str:
    try:
        result = subprocess.run(TOOLS[tool][1], capture_output=True, text=True, check=False)
    except OSError as error:
        return f"{TOOLS[tool][1][0]}: {error.strerror}"
    return result.stdout + result.stderr


def check(tools, pins, banner=run_banner, out=sys.stdout):
    """(exit code, {tool: version}): 1 when a tool is missing or its major.minor differs from the pin."""
    failed, versions = False, {}
    for tool in tools:
        text = banner(tool)
        version, expected = parse_version(tool, text), expected_version(tool, pins)
        first_line = next((line for line in text.splitlines() if line.strip()), "(no output)")
        print(f"{tool}: {first_line.strip()}", file=out)
        if version != expected:
            print(f"error: {tool} is {version or 'not found'}, toolchain.env pins {expected}. Update the runner or "
                  f"the pin in tools/ci/toolchain.env.", file=out)
            failed = True
        versions[tool] = version
    return (1 if failed else 0), versions


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tools", nargs="*", choices=sorted(TOOLS))
    parser.add_argument("--get", metavar="KEY", help="print one pinned value and exit")
    parser.add_argument("--pins", type=Path, default=DEFAULT_PINS)
    args = parser.parse_args(argv)
    pins = load_pins(args.pins)
    if args.get:
        print(pins[args.get])
        return 0
    code, versions = check(args.tools, pins)
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            for tool, version in versions.items():
                output.write(f"{tool}={version}\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
