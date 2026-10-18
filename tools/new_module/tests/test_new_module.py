# SPDX-License-Identifier: MIT
"""Unit tests for new_module.py (run by CTest as NewModule_PythonUnitTests)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import new_module  # noqa: E402


class NewModuleTests(unittest.TestCase):
    def scaffold(self, *extra: str) -> tuple[int, Path]:
        output = Path(self.temp.name) / "out"
        code = new_module.main(["--id", "com.author.fps", "--output", str(output), *extra])
        return code, output

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_module_writes_manifest_cmake_and_source(self) -> None:
        code, output = self.scaffold("--author", "Me")
        self.assertEqual(code, 0)
        manifest = json.loads((output / "module.json").read_text())
        self.assertEqual(manifest["id"], "com.author.fps")
        self.assertEqual(manifest["kind"], "Module")
        self.assertEqual(manifest["library"], "com_author_fps")
        self.assertEqual(manifest["name"], "Fps")
        self.assertNotIn("contracts", manifest)
        cmake = (output / "CMakeLists.txt").read_text()
        self.assertIn("find_package(Interi REQUIRED)", cmake)
        self.assertIn("interi_add_module_load_test(com_author_fps)", cmake)
        source = (output / "src" / "FpsModule.cpp").read_text()
        self.assertTrue(source.startswith("// SPDX-License-Identifier: MIT"))
        self.assertIn("INTERI_GAMEPLAY_MODULE(Register)", source)

    def test_extension_declares_contract(self) -> None:
        code, output = self.scaffold("--kind", "Extension", "--name", "Weapons")
        self.assertEqual(code, 0)
        manifest = json.loads((output / "module.json").read_text())
        self.assertEqual(manifest["contracts"], ["service.Weapons"])
        self.assertIn('"service.Weapons"', (output / "src" / "WeaponsModule.cpp").read_text())

    def test_plugin_has_overrides_section(self) -> None:
        code, output = self.scaffold("--kind", "Plugin")
        self.assertEqual(code, 0)
        self.assertEqual(json.loads((output / "module.json").read_text())["overrides"], [])

    def test_physics_engine_has_no_dependencies_and_provides_the_contract(self) -> None:
        code, output = self.scaffold("--kind", "PhysicsEngine", "--name", "Soft")
        self.assertEqual(code, 0)
        manifest = json.loads((output / "module.json").read_text())
        self.assertEqual(manifest["kind"], "PhysicsEngine")
        self.assertNotIn("dependencies", manifest)
        self.assertNotIn("extends", manifest)
        source = (output / "src" / "SoftModule.cpp").read_text()
        self.assertIn('"service.PhysicsQueries"', source)

    def test_physics_engine_extends(self) -> None:
        code, output = self.scaffold("--kind", "PhysicsEngine", "--extends", "com.interi.physics.jolt@^5")
        self.assertEqual(code, 0)
        manifest = json.loads((output / "module.json").read_text())
        self.assertEqual(manifest["extends"], {"id": "com.interi.physics.jolt", "version": "^5"})
        self.assertIn("com.interi.physics.jolt", (output / "src" / "FpsModule.cpp").read_text())
        cmake = (output / "CMakeLists.txt").read_text()
        self.assertIn("INTERI_BASE_ENGINE_DIR", cmake)
        self.assertIn("EXTRA_MODULE_DIRS", cmake)

    def test_extends_needs_a_physics_engine_and_a_valid_id(self) -> None:
        self.assertEqual(self.scaffold("--kind", "Module", "--extends", "com.a.b@^1")[0], 1)
        self.assertEqual(self.scaffold("--kind", "PhysicsEngine", "--extends", "NotAnId")[0], 1)
        self.assertEqual(self.scaffold("--kind", "PhysicsEngine", "--extends", "com.author.fps@^1")[0], 1)

    def test_invalid_id_fails(self) -> None:
        output = Path(self.temp.name) / "bad"
        self.assertEqual(new_module.main(["--id", "NotReverseDns", "--output", str(output)]), 1)
        self.assertFalse(output.exists())

    def test_non_empty_output_needs_force(self) -> None:
        code, output = self.scaffold()
        self.assertEqual(code, 0)
        self.assertEqual(self.scaffold()[0], 1)
        self.assertEqual(self.scaffold("--force")[0], 0)


if __name__ == "__main__":
    unittest.main()
