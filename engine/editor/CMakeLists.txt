# SPDX-License-Identifier: MIT
# Editor logic without UI: component reflection, the scene document (ECS entities), the level bridge, undo/redo commands, the
# inspector and outliner models, gizmo math, fracture authoring and structural debugging tools.
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor)

interi_add_module(editor
    SOURCES
        src/AssetBrowser.cpp
        src/Commands.cpp
        src/DataEditing.cpp
        src/EditorCamera.cpp
        src/EditorDocument.cpp
        src/EditorMath.cpp
        src/EditorScene.cpp
        src/EditorServices.cpp
        src/FracturePreview.cpp
        src/Gizmo.cpp
        src/HeaderCheck.cpp
        src/Inspector.cpp
        src/LevelBridge.cpp
        src/Modeling.cpp
        src/Picking.cpp
        src/Prefabs.cpp
        src/Projects.cpp
        src/PlaySession.cpp
        src/Reflection.cpp
        src/Replay.cpp
        src/ScenePlay.cpp
        src/StructureDebug.cpp
        src/UndoStack.cpp
    DEPS interi::modeling interi::scene interi::runtime interi::values interi::gameplay interi::destruction_cook interi::render interi::ecs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing interi::physics_testing
    TEST_SOURCES
        ${_tests}/DataEditingTests.cpp
        ${_tests}/EditorCameraTests.cpp
        ${_tests}/EditorDocumentTests.cpp
        ${_tests}/EditorSceneTests.cpp
        ${_tests}/EditorServicesTests.cpp
        ${_tests}/FracturePreviewTests.cpp
        ${_tests}/GizmoTests.cpp
        ${_tests}/InspectorTests.cpp
        ${_tests}/ModelingTests.cpp
        ${_tests}/PickingTests.cpp
        ${_tests}/PrefabsTests.cpp
        ${_tests}/PlaySessionTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/ScenePlayTests.cpp
        ${_tests}/UndoStackTests.cpp)
target_compile_definitions(interi_editor_tests PRIVATE
    INTERI_TEST_MATERIALS_FILE="${PROJECT_SOURCE_DIR}/data/destruction/materials.json"
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")

# Replays at 1 / 4 / 32 workers (label stress).
interi_add_test_executable(interi_editor_stress_tests
    SOURCES
        ${_tests}/ReplayStressTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
    DEPS interi::editor interi::testing interi::physics_testing
    LABELS stress)
