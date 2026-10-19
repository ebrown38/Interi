# SPDX-License-Identifier: MIT
# Editor logic without UI: component reflection, the scene document (ECS entities), undo/redo commands, the inspector and
# outliner models, gizmo math, modeling, prefabs, play-in-editor and the project and module services.
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor)

interi_add_module(editor
    SOURCES
        src/AssetBrowser.cpp
        src/ChainDebugger.cpp
        src/ChainEditing.cpp
        src/Commands.cpp
        src/DataEditing.cpp
        src/EditorCamera.cpp
        src/EditorDocument.cpp
        src/EditorModules.cpp
        src/EditorMath.cpp
        src/EditorScene.cpp
        src/EditorServices.cpp
        src/Gizmo.cpp
        src/HeaderCheck.cpp
        src/HudEditing.cpp
        src/Inspector.cpp
        src/Modeling.cpp
        src/ModuleManager.cpp
        src/Picking.cpp
        src/Prefabs.cpp
        src/Projects.cpp
        src/Reflection.cpp
        src/ScenePlay.cpp
        src/UndoStack.cpp
    DEPS interi::chain interi::modeling interi::scene interi::hud interi::game interi::runtime interi::values interi::render interi::ecs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ChainDebuggerTests.cpp
        ${_tests}/ChainEditingTests.cpp
        ${_tests}/DataEditingTests.cpp
        ${_tests}/EditorCameraTests.cpp
        ${_tests}/EditorDocumentTests.cpp
        ${_tests}/EditorModulesTests.cpp
        ${_tests}/EditorSceneTests.cpp
        ${_tests}/EditorServicesTests.cpp
        ${_tests}/GizmoTests.cpp
        ${_tests}/HudEditingTests.cpp
        ${_tests}/InspectorTests.cpp
        ${_tests}/ModelingTests.cpp
        ${_tests}/ModuleManagerTests.cpp
        ${_tests}/PickingTests.cpp
        ${_tests}/PrefabsTests.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/ScenePlayTests.cpp
        ${_tests}/UndoStackTests.cpp)
target_compile_definitions(interi_editor_tests PRIVATE
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")

if(INTERI_BUILD_SHARED)
    # The starter sample's module is what the editor hosts in EditorModulesTests.
    add_dependencies(interi_editor_tests com_interi_starter)
    target_compile_definitions(interi_editor_tests PRIVATE
        INTERI_STARTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/samples/modules/starter"
        INTERI_STARTER_LIBRARY_DIR="$<TARGET_FILE_DIR:com_interi_starter>")
endif()
target_include_directories(interi_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/chain)
