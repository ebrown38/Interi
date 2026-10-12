# SPDX-License-Identifier: MIT
"""License gate: every vcpkg dependency must carry a license from the policy allowlist.

Reads the SPDX documents vcpkg writes to <installed-dir>/share/<port>/vcpkg.spdx.json and
evaluates each port's licenseConcluded expression (AND / OR / WITH / parentheses).

Exit codes: 0 = all allowed, 1 = violations found, 2 = usage/input error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

DEFAULT_POLICY = Path(__file__).with_name("license_policy.json")


@dataclass
class Violation:
    port: str
    license: str | None
    reason: str


class _Parser:
    """Recursive-descent parser for SPDX license expressions.

    Grammar (SPDX 2.3 Annex D, precedence WITH > AND > OR):
        or_expr   := and_expr ("OR" and_expr)*
        and_expr  := with_expr ("AND" with_expr)*
        with_expr := primary ("WITH" exception_id)?
        primary   := license_id | "(" or_expr ")"
    Evaluates directly to a bool: is the expression satisfiable using only allowed licenses?
    """

    def __init__(self, text: str, allowed: set[str], exceptions: set[str]):
        self.tokens = re.findall(r"\(|\)|[^\s()]+", text)
        self.pos = 0
        self.allowed = allowed
        self.exceptions = exceptions

    def parse(self) -> bool:
        result = self._or()
        if self.pos != len(self.tokens):
            raise ValueError(f"unexpected token {self.tokens[self.pos]!r}")
        return result

    def _peek(self) -> str | None:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def _take(self) -> str:
        token = self._peek()
        if token is None:
            raise ValueError("unexpected end of expression")
        self.pos += 1
        return token

    def _or(self) -> bool:
        result = self._and()
        while self._peek() == "OR":
            self._take()
            rhs = self._and()  # always parse both sides so malformed input is still rejected
            result = result or rhs
        return result

    def _and(self) -> bool:
        result = self._with()
        while self._peek() == "AND":
            self._take()
            rhs = self._with()
            result = result and rhs
        return result

    def _with(self) -> bool:
        result = self._primary()
        if self._peek() == "WITH":
            self._take()
            exception = self._take()
            result = result and exception in self.exceptions
        return result

    def _primary(self) -> bool:
        token = self._take()
        if token == "(":
            result = self._or()
            if self._take() != ")":
                raise ValueError("missing ')'")
            return result
        if token in (")", "AND", "OR", "WITH"):
            raise ValueError(f"unexpected token {token!r}")
        return token in self.allowed


def is_allowed(expression: str | None, allowed_licenses, allowed_exceptions) -> bool:
    """True if the SPDX expression can be satisfied with allowlisted licenses only."""
    if not expression or expression.strip() in ("NOASSERTION", "NONE"):
        return False
    try:
        return _Parser(expression, set(allowed_licenses), set(allowed_exceptions)).parse()
    except ValueError:
        return False


def collect_ports(installed_dir: Path) -> dict[str, tuple[str | None, Path]]:
    """Map port name -> (licenseConcluded, share directory) from vcpkg's per-port SPDX documents."""
    ports: dict[str, tuple[str | None, Path]] = {}
    for spdx_file in sorted(Path(installed_dir).glob("share/*/vcpkg.spdx.json")):
        document = json.loads(spdx_file.read_text(encoding="utf-8"))
        # The document name ("<port>:<triplet>@<version> <hash>") is authoritative; the directory name is not on
        # case-insensitive file systems, where e.g. share/tracy and share/Tracy are the same directory.
        port = document.get("name", "").split(":", 1)[0] or spdx_file.parent.name
        packages = document.get("packages", [])
        package = next((p for p in packages if p.get("name") == port), packages[0] if packages else {})
        ports[port] = (package.get("licenseConcluded"), spdx_file.parent)
    return dict(sorted(ports.items()))


def collect_port_licenses(installed_dir: Path) -> dict[str, str | None]:
    """Map port name -> licenseConcluded from vcpkg's per-port SPDX documents."""
    return {port: license_expr for port, (license_expr, _) in collect_ports(installed_dir).items()}


def find_violations(installed_dir: Path, policy: dict) -> list[Violation]:
    allowed = policy["allowed_licenses"]
    exceptions = policy.get("allowed_exceptions", [])
    overrides = policy.get("overrides", {})
    violations = []
    for port, expression in collect_port_licenses(installed_dir).items():
        if port in overrides:
            # Overrides only fill in missing metadata after manual review; they are still checked.
            expression = overrides[port]["license"]
        if not is_allowed(expression, allowed, exceptions):
            violations.append(Violation(port, expression, "license not on the allowlist (or missing metadata)"))
    return violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--installed-dir", required=True, type=Path, help="vcpkg_installed/<triplet>")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--policy", type=Path, default=DEFAULT_POLICY, help="policy JSON file")
    group.add_argument("--policy-json", help="inline policy JSON (tests)")
    args = parser.parse_args(argv)

    policy = json.loads(args.policy_json) if args.policy_json else json.loads(args.policy.read_text(encoding="utf-8"))
    licenses = collect_port_licenses(args.installed_dir)
    if not licenses:
        print(f"error: no vcpkg SPDX documents found under {args.installed_dir}", file=sys.stderr)
        return 2

    violations = find_violations(args.installed_dir, policy)
    for port, expression in licenses.items():
        status = "FAIL" if any(v.port == port for v in violations) else "ok"
        print(f"  [{status}] {port}: {expression}")
    if violations:
        print(f"\n{len(violations)} dependency license violation(s). Interi is MIT open source; "
              "only permissive licenses from tools/license_check/license_policy.json are allowed.", file=sys.stderr)
        return 1
    print(f"All {len(licenses)} dependencies use allowed licenses.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
