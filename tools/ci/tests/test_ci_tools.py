# SPDX-License-Identifier: MIT
"""Unit tests for the CI helpers: toolchain pin check, OSV scan of the vcpkg SBOM, JUnit summary. Offline: git and
the OSV API are replaced by fakes."""
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_toolchain  # noqa: E402
import junit_summary  # noqa: E402
import osv_scan  # noqa: E402

PINS = {"CLANG": "22.1", "CLANG_FORMAT": "22.1", "MSVC_TOOLSET": "14.51"}
BANNERS = {
    "clang": "clang version 22.1.8 (Fedora 22.1.8-4.fc44)\nTarget: x86_64-redhat-linux-gnu\n",
    "clang-format": "clang-format version 22.1.8 (Fedora 22.1.8-4.fc44)\n",
    "msvc": "Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36231 for x64\nCopyright (C) Microsoft Corporation.\n",
}


class ToolchainTests(unittest.TestCase):
    def test_tracked_pins_parse(self):
        pins = check_toolchain.load_pins(check_toolchain.DEFAULT_PINS)
        self.assertEqual(set(pins), {"CLANG", "CLANG_FORMAT", "MSVC_TOOLSET"})

    def test_malformed_pin_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "toolchain.env"
            path.write_text("CLANG=latest\n")
            with self.assertRaises(ValueError):
                check_toolchain.load_pins(path)

    def test_msvc_toolset_maps_to_compiler_version(self):
        self.assertEqual(check_toolchain.expected_version("msvc", PINS), "19.51")

    def test_matching_versions_pass(self):
        code, versions = check_toolchain.check(["clang", "clang-format", "msvc"], PINS, BANNERS.get, io.StringIO())
        self.assertEqual(code, 0)
        self.assertEqual(versions, {"clang": "22.1", "clang-format": "22.1", "msvc": "19.51"})

    def test_other_minor_version_fails(self):
        out = io.StringIO()
        code, _ = check_toolchain.check(["clang"], PINS, lambda tool: "clang version 22.2.0\n", out)
        self.assertEqual(code, 1)
        self.assertIn("pins 22.1", out.getvalue())

    def test_missing_tool_fails(self):
        code, versions = check_toolchain.check(["msvc"], PINS, lambda tool: "cl: No such file or directory", io.StringIO())
        self.assertEqual(code, 1)
        self.assertIsNone(versions["msvc"])


def make_installed_dir(root: Path, ports: dict) -> Path:
    """Fake vcpkg_installed/<triplet>: share/<port>/vcpkg.spdx.json with the given source download locations."""
    for port, locations in ports.items():
        share = root / "share" / port
        share.mkdir(parents=True)
        packages = [{"name": port, "versionInfo": "1.0"}, {"name": f"{port}:x64-linux"}]
        packages += [{"name": location, "downloadLocation": location} for location in locations]
        (share / "vcpkg.spdx.json").write_text(json.dumps({"packages": packages}))
    return root


COMMIT_A = "a" * 40
COMMIT_B = "b" * 40
TAG_OBJECT = "c" * 40


def fake_ls_remote(repo, patterns):
    refs = {
        "https://github.com/x/annotated": {"refs/tags/v1": TAG_OBJECT, "refs/tags/v1^{}": COMMIT_A},
        "https://github.com/x/light": {"refs/tags/v2": COMMIT_B},
    }
    return {name: sha for name, sha in refs.get(repo, {}).items() if name in patterns}


class OsvScanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_sources_and_skipped_ports_are_read_from_the_sbom(self):
        installed = make_installed_dir(self.root, {
            "lib": ["git+https://github.com/x/light@v2"],
            "helper": [],
            "tool": ["https://github.com/x/tool/releases/download/v1/tool.tar.gz"],
        })
        sources, skipped = osv_scan.read_sources(installed)
        self.assertEqual(sources, [osv_scan.Source("lib", "1.0", "https://github.com/x/light", "v2")])
        self.assertEqual(sorted(skipped), ["helper", "tool"])

    def test_annotated_tag_resolves_to_its_peeled_commit(self):
        source = osv_scan.Source("p", "1", "https://github.com/x/annotated", "v1")
        self.assertEqual(osv_scan.resolve_commit(source, fake_ls_remote), COMMIT_A)

    def test_lightweight_tag_and_commit_resolve(self):
        self.assertEqual(osv_scan.resolve_commit(osv_scan.Source("p", "1", "https://github.com/x/light", "v2"),
                                                 fake_ls_remote), COMMIT_B)
        self.assertEqual(osv_scan.resolve_commit(osv_scan.Source("p", "1", "https://github.com/x/none", COMMIT_A),
                                                 fake_ls_remote), COMMIT_A)

    def test_unresolvable_ref_fails(self):
        with self.assertRaises(RuntimeError):
            osv_scan.resolve_commit(osv_scan.Source("p", "1", "https://github.com/x/light", "v9"), fake_ls_remote)

    def test_querybatch_pages_are_followed(self):
        calls = []

        def post(url, payload):
            calls.append(payload)
            if len(calls) == 1:
                return {"results": [{"vulns": [{"id": "OSV-1"}], "next_page_token": "t"}, {}]}
            self.assertEqual(payload, {"queries": [{"commit": COMMIT_A, "page_token": "t"}]})
            return {"results": [{"vulns": [{"id": "OSV-2"}]}]}

        results = osv_scan.query_osv([COMMIT_A, COMMIT_B], post)
        self.assertEqual(results, {COMMIT_A: ["OSV-1", "OSV-2"], COMMIT_B: []})
        self.assertEqual(len(calls), 2)

    def test_waivers_match_by_id_and_port(self):
        waivers = {"CVE-1": {"id": "CVE-1", "port": "lib", "reason": "not reachable"},
                   "CVE-9": {"id": "CVE-9", "port": "lib", "reason": "old"}}
        unwaived, waived, stale = osv_scan.evaluate([("lib", "CVE-1"), ("other", "CVE-1"), ("lib", "CVE-2")], waivers)
        self.assertEqual(unwaived, [("other", "CVE-1"), ("lib", "CVE-2")])
        self.assertEqual(waived, [("lib", "CVE-1", "not reachable")])
        self.assertEqual(stale, ["CVE-9"])

    def test_waiver_without_a_reason_is_rejected(self):
        path = self.root / "waivers.json"
        path.write_text(json.dumps({"waivers": [{"id": "CVE-1", "port": "lib"}]}))
        with self.assertRaises(ValueError):
            osv_scan.load_waivers(path)

    def test_tracked_waivers_load(self):
        self.assertTrue(all(w["reason"] for w in osv_scan.load_waivers(osv_scan.DEFAULT_WAIVERS).values()))

    def scan(self, waivers):
        installed = make_installed_dir(self.root, {"lib": ["git+https://github.com/x/light@v2"]})
        out = io.StringIO()

        def post(url, payload):
            return {"results": [{"vulns": [{"id": "CVE-1"}]}]}

        return osv_scan.scan([installed], waivers, fake_ls_remote, post, out), out.getvalue()

    def test_unwaived_advisory_fails_the_scan(self):
        code, out = self.scan({})
        self.assertEqual(code, 1)
        self.assertIn("error: lib: CVE-1", out)

    def test_waived_advisory_passes_the_scan(self):
        code, out = self.scan({"CVE-1": {"id": "CVE-1", "port": "lib", "reason": "unused header"}})
        self.assertEqual(code, 0)
        self.assertIn("No unwaived advisories", out)

    def test_no_sbom_fails_the_scan(self):
        self.assertEqual(osv_scan.scan([self.root], {}, fake_ls_remote, None, io.StringIO()), 1)


class JunitSummaryTests(unittest.TestCase):
    def test_counts_and_lists_failures(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "ctest.xml"
            path.write_text(
                '<testsuite tests="3">'
                '<testcase name="A.Pass" status="run"/>'
                '<testcase name="B.Fail" status="fail"><failure message="Failed"/></testcase>'
                '<testcase name="C.Skip" status="notrun"><skipped message="Disabled"/></testcase>'
                '</testsuite>')
            text = junit_summary.summarize("linux", path)
        self.assertIn("1 passed, 1 failed, 1 skipped", text)
        self.assertIn("`B.Fail`", text)

    def test_missing_file_is_reported(self):
        self.assertIn("No test results", junit_summary.summarize("linux", Path("/nonexistent/ctest.xml")))


if __name__ == "__main__":
    unittest.main()
