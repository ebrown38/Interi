# SPDX-License-Identifier: MIT
"""Checks that every Interi source file declares `SPDX-License-Identifier: MIT` near its top.

Exit codes: 0 = all files have the header, 1 = files missing it.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".inl", ".in", ".cmake", ".py", ".hlsl", ".hlsli"}
SOURCE_NAMES = {"CMakeLists.txt"}
DEFAULT_DIRS = ["cmake", "engine", "testing", "tests", "tools", "game"]
HEADER = "SPDX-License-Identifier: MIT"
MAX_HEADER_LINE = 5


def _is_source(path: Path) -> bool:
    return path.name in SOURCE_NAMES or path.suffix in SOURCE_SUFFIXES


def _has_header(path: Path) -> bool:
    with path.open(encoding="utf-8", errors="replace") as f:
        for _, line in zip(range(MAX_HEADER_LINE), f):
            if HEADER in line:
                return True
    return False


def find_missing_headers(root: Path, dirs: list[str]) -> list[Path]:
    missing = []
    candidates = [root / "CMakeLists.txt"] if (root / "CMakeLists.txt").exists() else []
    for d in dirs:
        if (root / d).is_dir():
            candidates.extend(p for p in (root / d).rglob("*") if p.is_file())
    for path in sorted(candidates):
        if _is_source(path) and "__pycache__" not in path.parts and not _has_header(path):
            missing.append(path)
    return missing


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)

    missing = find_missing_headers(args.root, DEFAULT_DIRS)
    for path in missing:
        print(f"missing '{HEADER}': {path.relative_to(args.root)}", file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
