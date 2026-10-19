# SPDX-License-Identifier: MIT
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import platform_audit  # noqa: E402


def make_tree(files: dict[str, str]) -> tempfile.TemporaryDirectory:
    temp = tempfile.TemporaryDirectory()
    for name, text in files.items():
        path = Path(temp.name) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return temp


def messages(files: dict[str, str]) -> list[str]:
    with make_tree(files) as root:
        return [str(v) for v in platform_audit.audit(Path(root))]


class PlatformAuditTests(unittest.TestCase):
    def test_real_repository_is_clean(self):
        root = Path(__file__).resolve().parents[3]
        self.assertEqual([str(v) for v in platform_audit.audit(root)], [])

    def test_vulkan_include_outside_its_module_fails(self):
        found = messages({"engine/render/src/Pass.cpp": "#include <vulkan/vulkan.h>\n"})
        self.assertEqual(len(found), 1)
        self.assertIn("Vulkan header", found[0])

    def test_vulkan_include_inside_its_module_passes(self):
        self.assertEqual(messages({"engine/rhi_vulkan/src/Device.cpp": "#include <volk.h>\nVkDevice d;\n"}), [])

    def test_vulkan_type_in_a_public_header_fails(self):
        found = messages({"engine/rhi/include/interi/rhi/Device.h": "struct S { VkBuffer b; };\n"})
        self.assertEqual(len(found), 1)
        self.assertIn("VkBuffer", found[0])

    def test_names_in_comments_and_strings_are_ignored(self):
        text = '// an SDL_Window* and a VkSurfaceKHR\n/* VkDevice */\nconst char* s = "SDL_Init";\n'
        self.assertEqual(messages({"engine/platform/include/interi/platform/W.h": text}), [])

    def test_sdl_outside_platform_sdl_fails(self):
        found = messages({"tools/editor/main.cpp": "#include <SDL3/SDL.h>\nSDL_Window* w;\n"})
        self.assertEqual(len(found), 2)

    def test_posix_header_in_a_public_header_fails_even_in_an_allowed_module(self):
        found = messages({"engine/platform/include/interi/platform/X.h": "#include <unistd.h>\n"})
        self.assertEqual(len(found), 1)
        self.assertIn("OS header", found[0])

    def test_posix_header_in_the_platform_layer_passes(self):
        self.assertEqual(messages({"engine/platform/src/DynamicLibrary.cpp": "#include <dlfcn.h>\n"}), [])

    def test_posix_header_in_gameplay_code_fails(self):
        found = messages({"engine/scene/src/Scene.cpp": "#include <sys/mman.h>\n"})
        self.assertEqual(len(found), 1)

    def test_windows_header_fails_outside_the_platform_layer(self):
        found = messages({"engine/ecs/src/World.cpp": "#if defined(_WIN32)\n#include <windows.h>\n#endif\n"})
        self.assertEqual(len(found), 1)

    def test_console_and_d3d12_headers_fail_everywhere(self):
        for header in ("d3d12.h", "dxgi1_6.h", "agc/agc.h", "gnm/gnm.h", "sce_kernel/kernel.h"):
            found = messages({"engine/rhi_vulkan/src/X.cpp": f"#include <{header}>\n"})
            self.assertEqual(len(found), 1, header)
            self.assertIn("external platform", found[0])

    def test_cmake_linking_vulkan_outside_its_module_fails(self):
        found = messages({"engine/audio/CMakeLists.txt": "target_link_libraries(x PRIVATE Vulkan::Vulkan)\n"})
        self.assertEqual(len(found), 1)

    def test_cmake_linking_a_backend_module_is_limited(self):
        self.assertEqual(len(messages({"engine/scene/CMakeLists.txt": "target_link_libraries(x PUBLIC interi::rhi_vulkan)\n"})), 1)
        self.assertEqual(messages({"tools/editor/CMakeLists.txt": "target_link_libraries(x PRIVATE interi::rhi_vulkan)\n"}), [])
        self.assertEqual(messages({"tests/render/CMakeLists.txt": "target_link_libraries(x PRIVATE interi::platform_sdl)\n"}), [])

    def test_cmake_comments_are_ignored(self):
        self.assertEqual(messages({"engine/audio/CMakeLists.txt": "# not Vulkan:: here\n"}), [])


if __name__ == "__main__":
    unittest.main()
