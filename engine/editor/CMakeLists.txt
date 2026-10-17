# SPDX-License-Identifier: MIT
# Editor logic without UI: component reflection, the level document (ECS entities), undo/redo commands, the
# inspector and outliner models, gizmo math, fracture authoring and structural debugging tools.
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor)

interi_add_module(editor
    SOURCES
        src/AssetBrowser.cpp
        src/Commands.cpp
        src/EditorCamera.cpp
        src/EditorDocument.cpp
        src/EditorMath.cpp
        src/EditorScene.cpp
        src/FracturePreview.cpp
        src/Gizmo.cpp
        src/HeaderCheck.cpp
        src/Inspector.cpp
        src/Picking.cpp
        src/Reflection.cpp
        src/UndoStack.cpp
    DEPS interi::gameplay interi::destruction_cook interi::render interi::ecs interi::platform interi::core
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/EditorCameraTests.cpp
        ${_tests}/EditorDocumentTests.cpp
        ${_tests}/EditorSceneTests.cpp
        ${_tests}/FracturePreviewTests.cpp
        ${_tests}/GizmoTests.cpp
        ${_tests}/InspectorTests.cpp
        ${_tests}/PickingTests.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/UndoStackTests.cpp)
target_compile_definitions(interi_editor_tests PRIVATE
    INTERI_TEST_MATERIALS_FILE="${PROJECT_SOURCE_DIR}/data/destruction/materials.json")
