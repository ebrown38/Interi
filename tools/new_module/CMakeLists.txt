# SPDX-License-Identifier: MIT
# new_module.py scaffolds Modules, Extensions and Plugins; tests/package builds its output against the installed
# engine.
add_test(NAME NewModule_PythonUnitTests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_new_module
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set_tests_properties(NewModule_PythonUnitTests PROPERTIES LABELS unit)
