# SPDX-License-Identifier: MIT
# Editor logic without UI: component reflection, the level document (ECS entities), undo/redo commands, the
# inspector and outliner models, gizmo math, fracture authoring and structural debugging tools.
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor)

interi_add_module(editor
    SOURCES
        src/Commands.cpp
        src/EditorScene.cpp
        src/HeaderCheck.cpp
        src/Inspector.cpp
        src/Reflection.cpp
        src/UndoStack.cpp
    DEPS interi::gameplay interi::ecs interi::core
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/EditorSceneTests.cpp
        ${_tests}/InspectorTests.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/UndoStackTests.cpp)
