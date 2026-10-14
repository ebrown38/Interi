# SPDX-License-Identifier: MIT
"""Unit tests for the license gate, notices generator and SPDX header checker."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_licenses  # noqa: E402
import check_spdx_headers  # noqa: E402
import gen_notices  # noqa: E402

POLICY = {
    "allowed_licenses": ["MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "Zlib"],
    "allowed_exceptions": ["LLVM-exception"],
    "overrides": {},
}


def make_installed_dir(root: Path, ports: dict) -> Path:
    """Fake vcpkg_installed/<triplet> tree: share/<port>/{vcpkg.spdx.json,copyright}."""
    for port, license_expr in ports.items():
        share = root / "share" / port
        share.mkdir(parents=True)
        spdx = {"packages": [{"name": port, "licenseConcluded": license_expr, "licenseDeclared": "NOASSERTION"}]}
        (share / "vcpkg.spdx.json").write_text(json.dumps(spdx))
        (share / "copyright").write_text(f"Copyright text of {port}\n")
    return root


class SpdxExpressionTests(unittest.TestCase):
    def allowed(self, expr):
        return check_licenses.is_allowed(expr, POLICY["allowed_licenses"], POLICY["allowed_exceptions"])

    def test_single_allowed_license(self):
        self.assertTrue(self.allowed("MIT"))

    def test_single_disallowed_license(self):
        self.assertFalse(self.allowed("GPL-3.0-only"))

    def test_or_needs_one_allowed_branch(self):
        self.assertTrue(self.allowed("GPL-2.0-only OR MIT"))
        self.assertFalse(self.allowed("GPL-2.0-only OR LGPL-2.1-only"))

    def test_and_needs_all_branches_allowed(self):
        self.assertTrue(self.allowed("MIT AND BSD-3-Clause"))
        self.assertFalse(self.allowed("MIT AND GPL-3.0-only"))

    def test_with_exception_requires_allowed_exception(self):
        self.assertTrue(self.allowed("Apache-2.0 WITH LLVM-exception"))
        self.assertFalse(self.allowed("Apache-2.0 WITH Classpath-exception-2.0"))

    def test_parentheses_and_precedence(self):
        # AND binds tighter than OR: "GPL OR (MIT AND Zlib)".
        self.assertTrue(self.allowed("GPL-3.0-only OR MIT AND Zlib"))
        self.assertFalse(self.allowed("(GPL-3.0-only OR MIT) AND LGPL-2.1-only"))

    def test_noassertion_and_empty_are_not_allowed(self):
        self.assertFalse(self.allowed("NOASSERTION"))
        self.assertFalse(self.allowed(""))
        self.assertFalse(self.allowed(None))

    def test_malformed_expression_is_not_allowed(self):
        self.assertFalse(self.allowed("MIT AND"))
        self.assertFalse(self.allowed("(MIT"))


class CheckLicensesTests(unittest.TestCase):
    def test_all_deps_on_allowlist_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"gtest": "BSD-3-Clause", "jolt": "MIT"})
            self.assertEqual(check_licenses.find_violations(installed, POLICY), [])

    def test_gpl_dep_injected_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"gtest": "BSD-3-Clause", "evil": "GPL-3.0-or-later"})
            violations = check_licenses.find_violations(installed, POLICY)
            self.assertEqual([v.port for v in violations], ["evil"])
            self.assertEqual(check_licenses.main(["--installed-dir", tmp, "--policy-json", json.dumps(POLICY)]), 1)

    def test_missing_license_metadata_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"mystery": "NOASSERTION"})
            self.assertEqual([v.port for v in check_licenses.find_violations(installed, POLICY)], ["mystery"])

    def test_override_with_reason_allows_reviewed_port(self):
        policy = dict(POLICY, overrides={"mystery": {"license": "MIT", "reason": "upstream LICENSE reviewed"}})
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"mystery": "NOASSERTION"})
            self.assertEqual(check_licenses.find_violations(installed, policy), [])

    def test_override_cannot_launder_disallowed_license(self):
        policy = dict(POLICY, overrides={"evil": {"license": "GPL-3.0-only", "reason": "x"}})
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"evil": "NOASSERTION"})
            self.assertEqual(len(check_licenses.find_violations(installed, policy)), 1)

    def test_empty_installed_dir_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(check_licenses.main(["--installed-dir", tmp, "--policy-json", json.dumps(POLICY)]), 2)


class NoticesTests(unittest.TestCase):
    def test_notices_list_ports_sorted_with_license_and_copyright(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"zlib-port": "Zlib", "alpha": "MIT"})
            text = gen_notices.generate_notices(installed)
            self.assertLess(text.index("## alpha"), text.index("## zlib-port"))
            self.assertIn("License: MIT", text)
            self.assertIn("Copyright text of alpha", text)

    def test_vcpkg_helper_ports_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"vcpkg-cmake": "MIT", "gtest": "BSD-3-Clause"})
            text = gen_notices.generate_notices(installed)
            self.assertNotIn("vcpkg-cmake", text)
            self.assertIn("## gtest", text)

    def test_port_name_comes_from_spdx_document_not_directory_case(self):
        # On case-insensitive file systems (Windows) vcpkg's share/tracy and share/Tracy (CMake config) are one
        # directory, which may be spelled "Tracy". The SPDX document names the port authoritatively.
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"Tracy": "BSD-3-Clause", "benchmark": "Apache-2.0"})
            spdx_file = installed / "share" / "Tracy" / "vcpkg.spdx.json"
            spdx = json.loads(spdx_file.read_text())
            spdx["name"] = "tracy:x64-windows@0.14.1#1 0123abcd"
            spdx["packages"][0]["name"] = "tracy"
            spdx_file.write_text(json.dumps(spdx))
            self.assertEqual(check_licenses.collect_port_licenses(installed),
                             {"benchmark": "Apache-2.0", "tracy": "BSD-3-Clause"})
            text = gen_notices.generate_notices(installed)
            self.assertLess(text.index("## benchmark"), text.index("## tracy"))

    def test_empty_packages_excluded(self):
        # e.g. vcpkg's `pthreads` on Linux: metadata only, no copyright file, nothing shipped. Listing it would make
        # the notices differ between platforms where the port is or isn't in the dependency graph.
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"pthreads": "Apache-2.0", "gtest": "BSD-3-Clause"})
            (installed / "share" / "pthreads" / "copyright").unlink()
            text = gen_notices.generate_notices(installed)
            self.assertNotIn("pthreads", text)
            self.assertIn("## gtest", text)

    def test_build_tools_excluded(self):
        # Host-only build tools (e.g. the shader compiler) are license-checked but never shipped, and vcpkg installs
        # them into the target triplet only when host == target, so listing them would differ between platforms.
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp), {"directx-dxc": "NOASSERTION", "gtest": "BSD-3-Clause"})
            text = gen_notices.generate_notices(installed, build_tools={"directx-dxc"})
            self.assertNotIn("directx-dxc", text)
            self.assertIn("## gtest", text)

    def test_check_mode_detects_stale_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            installed = make_installed_dir(Path(tmp) / "installed", {"gtest": "BSD-3-Clause"})
            output = Path(tmp) / "NOTICES.md"
            args = ["--installed-dir", str(installed), "--output", str(output)]
            self.assertEqual(gen_notices.main(args), 0)
            self.assertEqual(gen_notices.main(args + ["--check"]), 0)
            output.write_text("stale\n")
            self.assertEqual(gen_notices.main(args + ["--check"]), 1)


class SpdxHeaderTests(unittest.TestCase):
    def test_file_without_header_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "engine").mkdir()
            (root / "engine" / "good.cpp").write_text("// SPDX-License-Identifier: MIT\nint x;\n")
            (root / "engine" / "bad.h").write_text("#pragma once\n")
            (root / "engine" / "notes.txt").write_text("not source\n")
            missing = check_spdx_headers.find_missing_headers(root, ["engine"])
            self.assertEqual([p.name for p in missing], ["bad.h"])

    def test_header_must_be_near_top(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "tools").mkdir()
            (root / "tools" / "late.py").write_text("\n" * 10 + "# SPDX-License-Identifier: MIT\n")
            self.assertEqual(len(check_spdx_headers.find_missing_headers(root, ["tools"])), 1)

    def test_cmakelists_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "engine").mkdir()
            (root / "engine" / "CMakeLists.txt").write_text("add_subdirectory(x)\n")
            self.assertEqual(len(check_spdx_headers.find_missing_headers(root, ["engine"])), 1)

    def test_wrong_license_identifier_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "engine").mkdir()
            (root / "engine" / "gpl.cpp").write_text("// SPDX-License-Identifier: GPL-3.0-only\n")
            self.assertEqual(len(check_spdx_headers.find_missing_headers(root, ["engine"])), 1)


if __name__ == "__main__":
    unittest.main()
