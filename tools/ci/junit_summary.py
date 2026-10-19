# SPDX-License-Identifier: MIT
"""Markdown summary of a CTest JUnit file (`ctest --output-junit`), for $GITHUB_STEP_SUMMARY.

Usage: junit_summary.py <title> <ctest-junit.xml>
"""
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def summarize(title: str, path: Path) -> str:
    if not path.exists():
        return f"### {title}\n\nNo test results (`{path}` was not written).\n"
    root = ElementTree.parse(path).getroot()
    cases = root.iter("testcase")
    passed, skipped, failed = 0, 0, []
    for case in cases:
        if case.find("failure") is not None or case.find("error") is not None:
            failed.append(case.get("name", "?"))
        elif case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
            skipped += 1
        else:
            passed += 1
    lines = [f"### {title}", "", f"{passed} passed, {len(failed)} failed, {skipped} skipped", ""]
    lines += [f"- :x: `{name}`" for name in sorted(failed)]
    return "\n".join(lines) + "\n"


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    sys.stdout.write(summarize(argv[0], Path(argv[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
