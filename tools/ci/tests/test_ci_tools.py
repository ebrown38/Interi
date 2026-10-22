# SPDX-License-Identifier: MIT
"""Unit tests for the CI helpers: toolchain pin check, OSV scan of the vcpkg SBOM, JUnit summary, test selection.
Offline: git, the OSV API, Ninja and CTest are replaced by fakes."""
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
import select_tests  # noqa: E402

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


SOURCE, BUILD = Path("/s"), Path("/s/build")
BUILD_NINJA = """# a CMake-style manifest in miniature
include CMakeFiles/rules.ninja
build engine/core/CMakeFiles/core.dir/A.cpp.o: CXX_COMPILER__core_Debug /s/engine/core/src/A.cpp || order_core
  FLAGS = -O0
build engine/core/libinteri_core.so: CXX_SHARED_LIBRARY_LINKER__core_Debug engine/core/CMakeFiles/core.dir/A.cpp.o
build tests/CMakeFiles/core_tests.dir/T.cpp.o: CXX_COMPILER__core_tests_Debug /s/tests/core/T.cpp
build tests/core_tests: CXX_EXECUTABLE_LINKER__core_tests_Debug tests/CMakeFiles/core_tests.dir/T.cpp.o $
    | engine/core/libinteri_core.so || engine/plug/libplug.so
build engine/plug/CMakeFiles/plug.dir/P.cpp.o: CXX_COMPILER__plug_Debug /s/tests/plug/P.cpp
build engine/plug/libplug.so: CXX_MODULE_LIBRARY_LINKER__plug_Debug engine/plug/CMakeFiles/plug.dir/P.cpp.o
build vk/CMakeFiles/vk.dir/V$ Space.cpp.o: CXX_COMPILER__vk_Debug /s/engine/vk/V$ Space.cpp
build vk/vk_tests: CXX_EXECUTABLE_LINKER__vk_Debug vk/CMakeFiles/vk.dir/V$ Space.cpp.o |@ validation_stamp
build build.ninja: RERUN_CMAKE | /s/CMakeLists.txt /s/engine/core/Version.cpp.in $
    /usr/share/cmake/Modules/X.cmake
"""
NINJA_DEPS = """engine/core/CMakeFiles/core.dir/A.cpp.o: #deps 2, deps mtime 1 (VALID)
    /s/engine/core/src/A.cpp
    /s/engine/core/include/interi/core/A.h

tests/CMakeFiles/core_tests.dir/T.cpp.o: #deps 1, deps mtime 1 (VALID)
    /s/engine/core/include/interi/core/A.h
"""
INSTALL_SCRIPT = """if(CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE DIRECTORY FILES "/s/engine/core/include/")
  file(INSTALL DESTINATION "include" TYPE DIRECTORY FILES "/s/build/gen/" FILES_MATCHING REGEX "x")
  file(INSTALL DESTINATION "lib64" TYPE SHARED_LIBRARY FILES "/s/build/engine/core/libinteri_core.so")
endif()
"""

COMPILE_COMMANDS = json.dumps([
    {"directory": "/s/build", "output": "vk/CMakeFiles/vk.dir/V$ Space.cpp.o".replace("$ ", " "),
     "command": 'c++ -DPLUGIN=\\"/s/build/engine/plug/libplug.so\\" -DOUT=\\"/tmp/x\\" -c /s/engine/vk/V.cpp'},
    {"directory": "/s/build", "output": "tests/CMakeFiles/core_tests.dir/T.cpp.o",
     "command": 'c++ -DLIBS=\\"/s/build/engine/core\\" -c /s/tests/core/T.cpp'},
])


def ctest_entry(name, command, working_dir=None, **fixtures):
    properties = [{"name": "LABELS", "value": ["unit"]}]
    if working_dir:
        properties.append({"name": "WORKING_DIRECTORY", "value": working_dir})
    properties += [{"name": key, "value": value} for key, value in fixtures.items()]
    return {"name": name, "command": command, "properties": properties}


CTEST_JSON = json.dumps({"kind": "ctestInfo", "tests": [
    ctest_entry("Core.One", ["/s/build/tests/core_tests", "--gtest_filter=Core.One"], "/s/build/tests"),
    ctest_entry("Vk.One", ["/s/build/vk/vk_tests", "--gtest_filter=Vk.One"], "/s/build/vk"),
    ctest_entry("CiTools", ["/usr/bin/python3", "-m", "unittest"], "/s/tools/ci/tests"),
    ctest_entry("Spdx", ["/usr/bin/python3", "/s/tools/license_check/check.py", "--root", "/s"]),
    ctest_entry("Notices", ["/usr/bin/python3", "/s/tools/license_check/notices.py", "--output", "/s/NOTICES.md"]),
    ctest_entry("Package_Install", ["/usr/bin/cmake", "--install", "/s/build", "--prefix", "/s/build/pkg/install"],
                FIXTURES_SETUP="pkg"),
    ctest_entry("Package_Starter", ["/usr/bin/cmake", "-DSOURCE=/s/samples/starter", "-DPREFIX=/s/build/pkg|/x",
                                    "-P", "/s/tests/package/Build.cmake"], FIXTURES_REQUIRED=["pkg"]),
]})


class SelectTestsTests(unittest.TestCase):
    def setUp(self):
        self.graph = select_tests.Graph()
        select_tests.parse_ninja(BUILD_NINJA, BUILD, self.graph, read=lambda path: "rule CXX\n  command = c++\n")
        select_tests.parse_deps(NINJA_DEPS, BUILD, self.graph)
        select_tests.parse_compile_commands(COMPILE_COMMANDS, BUILD, self.graph)
        self.tests = select_tests.parse_ctest_json(CTEST_JSON)
        self.installed = select_tests.parse_install_script(INSTALL_SCRIPT, BUILD)

    def choose(self, *changes):
        return select_tests.select([tuple(change.split("\t")) for change in changes], self.tests, self.graph, SOURCE,
                                   BUILD, self.installed)

    def test_header_selects_its_dependents_the_package_and_repo_scans(self):
        selection = self.choose("M\tengine/core/include/interi/core/A.h")
        self.assertEqual(selection.full_reason, "")
        self.assertEqual(selection.names, ["Core.One", "Package_Install", "Package_Starter", "Spdx"])

    def test_runtime_loaded_module_selects_through_order_only_edge_or_compiled_path(self):
        self.assertEqual(self.choose("M\ttests/plug/P.cpp").names, ["Core.One", "Spdx", "Vk.One"])

    def test_compiled_build_directory_depends_on_what_is_linked_there(self):
        self.assertIn(select_tests.normalize("/s/build/tests/CMakeFiles/core_tests.dir/T.cpp.o", BUILD),
                      self.graph.dependents["/s/build/engine/core/libinteri_core.so"])
        self.assertNotIn("/tmp/x", self.graph.dependents)

    def test_files_next_to_a_named_script_select_it(self):
        selection = self.choose("M\ttools/license_check/policy.json")
        self.assertEqual((selection.full_reason, selection.names), ("", ["Notices", "Spdx"]))

    def test_repository_root_files_match_exactly(self):
        self.assertEqual(self.choose("M\tNOTICES.md").names, ["Notices", "Spdx"])
        self.assertEqual(self.choose("M\tOTHER.md").names, ["Spdx"])

    def test_escaped_paths_parse(self):
        self.assertEqual(self.choose("A\tengine/vk/V Space.cpp").names, ["Spdx", "Vk.One"])

    def test_docs_select_only_repository_scans(self):
        self.assertEqual(self.choose("M\tdocs/guide.md", "D\tREADME.md").names, ["Spdx"])

    def test_python_tool_selects_its_unit_tests(self):
        selection = self.choose("M\ttools/ci/osv_scan.py")
        self.assertEqual((selection.full_reason, selection.names), ("", ["CiTools", "Spdx"]))

    def test_build_configuration_runs_everything(self):
        everything = sorted(t.name for t in self.tests)
        for change in ("M\tengine/core/CMakeLists.txt", "M\tengine/core/Version.cpp.in", "M\t.github/workflows/ci.yml",
                       "M\tcmake/InteriModule.cmake", "M\texternal/vcpkg"):
            selection = self.choose(change)
            self.assertEqual(selection.names, everything, change)
            self.assertNotEqual(selection.full_reason, "", change)

    def test_untraced_data_runs_everything(self):
        selection = self.choose("M\tsamples/starter/project.json")
        self.assertEqual(selection.untraced, ["samples/starter/project.json"])
        self.assertEqual(len(selection.names), len(self.tests))

    def test_deleted_source_runs_everything(self):
        self.assertIn("deleted", self.choose("D\ttests/core/Old.cpp").full_reason)

    def test_link_edge_validations_are_not_inputs(self):
        self.assertNotIn("/s/build/validation_stamp", self.graph.dependents)

    def test_regeneration_inputs_span_continuation_lines(self):
        self.assertIn(select_tests.normalize("/usr/share/cmake/Modules/X.cmake", BUILD), self.graph.regenerate)

    def test_install_script_lists_files_and_directories(self):
        self.assertEqual(self.installed, {"/s/engine/core/include", "/s/build/gen",
                                          "/s/build/engine/core/libinteri_core.so"})

    def test_windows_relative_outputs_normalize(self):
        self.assertEqual(select_tests.normalize("bin\\core_tests.exe", BUILD), "/s/build/bin/core_tests.exe")

    def test_change_markers_parse(self):
        self.assertEqual(select_tests.parse_changes("ALL\tschedule event"), (None, "schedule event"))
        self.assertEqual(select_tests.parse_changes("NONE\n"), ([], ""))
        self.assertEqual(select_tests.parse_changes("M\ta.cpp\nA\tb.h\n"), ([("M", "a.cpp"), ("A", "b.h")], ""))
        self.assertEqual(self.choose().names, [])


class FakeGit:
    def __init__(self, diff="M\ta.cpp\n", commits=("before1",), merge_base="base1", head="head1"):
        self.diff, self.commits, self.merge_base, self.head, self.calls = diff, commits, merge_base, head, []

    def __call__(self, *args):
        self.calls.append(args)
        if args[0] == "cat-file":
            return "" if args[2].split("^")[0] in self.commits else None
        if args[0] == "merge-base":
            return self.merge_base
        if args[0] == "rev-parse":
            return self.head + "\n"
        return self.diff


class ChangedFilesTests(unittest.TestCase):
    def changes(self, event, git, base="", before="", branch="main", ref_type="branch"):
        return select_tests.changed_files(event, base, before, branch, "main", git, ref_type)

    def test_pull_request_diffs_against_base(self):
        git = FakeGit()
        self.assertEqual(self.changes("pull_request", git, base="base9"), ["M\ta.cpp"])
        self.assertIn(("diff", "--name-status", "--no-renames", "base9", "HEAD"), git.calls)

    def test_default_branch_push_diffs_against_before(self):
        git = FakeGit()
        self.changes("push", git, before="before1")
        self.assertIn(("diff", "--name-status", "--no-renames", "before1", "HEAD"), git.calls)

    def test_branch_push_diffs_against_default_branch(self):
        git = FakeGit()
        self.changes("push", git, before="before1", branch="feature")  # earlier pushes may have been cancelled
        self.assertIn(("merge-base", "origin/main", "HEAD"), git.calls)
        self.assertIn(("diff", "--name-status", "--no-renames", "base1", "HEAD"), git.calls)

    def test_unknown_history_runs_everything(self):
        for before in ("gone", "0" * 40, ""):
            self.assertTrue(self.changes("push", FakeGit(commits=()), before=before)[0].startswith("ALL\t"), before)
        self.assertTrue(self.changes("push", FakeGit(merge_base=None), branch="feature")[0].startswith("ALL\t"))

    def test_scheduled_and_failed_and_huge_runs_run_everything(self):
        self.assertEqual(self.changes("schedule", FakeGit()), ["ALL\tschedule event"])
        self.assertTrue(self.changes("pull_request", FakeGit(diff=None), base="b")[0].startswith("ALL\t"))
        huge = "".join(f"M\tengine/core/src/File{i}.cpp\n" for i in range(2000))  # over the environment's limit
        self.assertTrue(self.changes("pull_request", FakeGit(diff=huge), base="b")[0].startswith("ALL\t"))

    def test_tag_push_runs_everything(self):  # a tag on the default branch's head would otherwise diff as empty
        for branch in ("v1.0", "main"):
            self.assertEqual(self.changes("push", FakeGit(), before="before1", branch=branch, ref_type="tag"),
                             ["ALL\ttag push"], branch)

    def test_empty_change_is_marked(self):
        self.assertEqual(self.changes("pull_request", FakeGit(diff=""), base="b"), ["NONE"])

if __name__ == "__main__":
    unittest.main()
