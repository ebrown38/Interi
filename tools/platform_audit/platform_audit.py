#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Platform audit: backend and OS types stay inside the modules that wrap them.

The engine is platform-neutral so a console port can live in a separate repository (see
cmake/InteriExternalPlatform.cmake). This script fails when, outside the allowed folders,

  * a source or header includes a Vulkan, SDL, POSIX or Win32 header, or names a Vulkan/SDL type;
  * any file names a console SDK or D3D12 header (those are only ever in an external platform folder);
  * a CMakeLists.txt links a Vulkan or SDL library, or the backend modules, directly.

Usage: platform_audit.py [--root <repo>]   (exit 0: clean, 1: violations, 2: usage)
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SCAN_DIRS = ("engine", "tools", "tests", "testing", "benchmarks", "samples")
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inl"}

# Folders (prefixes of repo-relative POSIX paths) that may use each family.
VULKAN_HOME = ("engine/rhi_vulkan/", "tests/rhi_vulkan/", "engine/platform_sdl/", "tests/platform_sdl/")
SDL_HOME = ("engine/platform_sdl/", "tests/platform_sdl/")
# OS headers: the thin layers over the OS, and the tests that exercise them. Never a public header.
OS_HOME = (
    "engine/platform/src/",
    "engine/platform_sdl/",
    "engine/rhi_vulkan/",
    "engine/jobs/src/JobSystem.cpp",
    "tests/jobs/JobSystemTests.cpp",
    "tests/platform/",
    "tests/rhi_vulkan/",
    "tests/platform_sdl/",
    "tests/samples/StarterExportTests.cpp",  # runs interi_export / interi_player as processes
)
# CMakeLists.txt files that may link the backend modules (the editor and GPU tests are built on them).
BACKEND_LINK_HOME = (
    "engine/rhi_vulkan/",
    "engine/platform_sdl/",
    "engine/render/CMakeLists.txt",
    "engine/editor_ui/CMakeLists.txt",
    "tools/editor/CMakeLists.txt",
    "tools/player/CMakeLists.txt",
    "benchmarks/render/CMakeLists.txt",  # GPU benchmarks
    "tests/",
)

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
VULKAN_HEADERS = re.compile(r"^(vulkan/|vulkan\.h$|volk\.h$|vk_mem_alloc\.h$|spirv_reflect\.h$|spirv-reflect/)")
SDL_HEADERS = re.compile(r"^(SDL3?/|SDL3?\.h$|SDL_)")
OS_HEADERS = re.compile(
    r"^(unistd\.h|sys/.+|pthread\.h|dlfcn\.h|fcntl\.h|poll\.h|signal\.h|dirent\.h|semaphore\.h|netinet/.+|arpa/.+|"
    r"windows\.h|winsock2?\.h|ws2tcpip\.h|wrl/.+|wrl\.h)$",
    re.IGNORECASE,
)
EXTERNAL_ONLY_HEADERS = re.compile(
    r"^(d3d1[12]\w*\.h|d3d12/.+|dxgi\w*\.h|agc/.+|gnm/.+|gnmx/.+|sce\w*/.+|kernel\.h|nvn/.+|xbox\w*/.+|"
    r"GameInput\.h|xg\.h|xsapi/.+)$",
    re.IGNORECASE,
)
VULKAN_TYPE = re.compile(r"\bVk[A-Z][A-Za-z0-9_]*\b|\bVK_[A-Z0-9_]{3,}\b|\bvk[A-Z][A-Za-z0-9]*\s*\(")
SDL_TYPE = re.compile(r"\bSDL_[A-Za-z0-9_]+\b")
CMAKE_BACKEND_LIB = re.compile(r"Vulkan::|\bvolk::|unofficial-vulkan|SDL3::|SDL3-|\bSDL3\b|VulkanMemoryAllocator|spirv-reflect",
                               re.IGNORECASE)
CMAKE_BACKEND_MODULE = re.compile(r"interi::(rhi_vulkan|platform_sdl)\b")


@dataclass(frozen=True)
class Violation:
    path: str
    line: int
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.message}"


def strip_comments(text: str) -> str:
    """Blank out comments and string literals (keeping newlines) so identifiers in them don't count."""

    def blank(match: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", match.group(0))

    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", blank, text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', blank, text)


def allowed(path: str, homes: tuple[str, ...]) -> bool:
    return any(path.startswith(home) for home in homes)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def audit_source(path: str, text: str) -> list[Violation]:
    found: list[Violation] = []
    is_public_header = "/include/" in path
    for match in INCLUDE.finditer(text):
        header = match.group(1)
        where = line_of(text, match.start())
        if EXTERNAL_ONLY_HEADERS.match(header):
            found.append(Violation(path, where, f"'{header}' belongs in an external platform folder, not this repo"))
        elif VULKAN_HEADERS.match(header) and not allowed(path, VULKAN_HOME):
            found.append(Violation(path, where, f"Vulkan header '{header}' outside the Vulkan modules"))
        elif SDL_HEADERS.match(header) and not allowed(path, SDL_HOME):
            found.append(Violation(path, where, f"SDL header '{header}' outside platform_sdl"))
        elif OS_HEADERS.match(header) and (is_public_header or not allowed(path, OS_HOME)):
            found.append(Violation(path, where, f"OS header '{header}' outside the platform layers"))
    code = strip_comments(text)
    if not allowed(path, VULKAN_HOME):
        for match in VULKAN_TYPE.finditer(code):
            found.append(Violation(path, line_of(code, match.start()), f"Vulkan name '{match.group(0).strip()}'"))
    if not allowed(path, SDL_HOME):
        for match in SDL_TYPE.finditer(code):
            found.append(Violation(path, line_of(code, match.start()), f"SDL name '{match.group(0)}'"))
    return found


def audit_cmake(path: str, text: str) -> list[Violation]:
    found: list[Violation] = []
    lines = text.splitlines()
    for number, raw in enumerate(lines, start=1):
        line = raw.split("#", 1)[0]
        if CMAKE_BACKEND_LIB.search(line) and not allowed(path, VULKAN_HOME + SDL_HOME):
            found.append(Violation(path, number, "links a Vulkan/SDL library outside the backend modules"))
        elif CMAKE_BACKEND_MODULE.search(line) and not allowed(path, BACKEND_LINK_HOME):
            found.append(Violation(path, number, "links a backend module (rhi_vulkan/platform_sdl) directly"))
    return found


def audit(root: Path) -> list[Violation]:
    violations: list[Violation] = []
    for top in SCAN_DIRS:
        base = root / top
        if not base.is_dir():
            continue
        for file in sorted(base.rglob("*")):
            if not file.is_file():
                continue
            relative = file.relative_to(root).as_posix()
            if "/build/" in f"/{relative}" or "/__pycache__/" in relative:
                continue
            if file.name == "CMakeLists.txt" or file.suffix == ".cmake":
                violations += audit_cmake(relative, file.read_text(encoding="utf-8", errors="replace"))
            elif file.suffix in SOURCE_SUFFIXES:
                violations += audit_source(relative, file.read_text(encoding="utf-8", errors="replace"))
    return violations


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    if not args.root.is_dir():
        print(f"error: {args.root} is not a directory", file=sys.stderr)
        return 2
    violations = audit(args.root)
    for violation in violations:
        print(violation)
    if violations:
        print(f"platform audit: {len(violations)} violation(s)", file=sys.stderr)
        return 1
    print("platform audit: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
