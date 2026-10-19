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
        src/MaterialEditing.cpp
        src/MaterialPreview.cpp
        src/Modeling.cpp
        src/ModuleConflicts.cpp
        src/ModuleManager.cpp
        src/Picking.cpp
        src/PluginAuthoring.cpp
        src/Prefabs.cpp
        src/Projects.cpp
        src/Reflection.cpp
        src/ScenePlay.cpp
        src/Thumbnails.cpp
        src/UndoStack.cpp
    DEPS interi::asset_import interi::assets interi::chain interi::hotreload interi::sdk interi::modeling interi::scene interi::hud interi::game interi::runtime interi::values interi::render interi::ecs interi::platform interi::core
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
        ${_tests}/ModelInstancingTests.cpp
        ${_tests}/AssetBrowserTests.cpp
        ${_tests}/MaterialEditingTests.cpp
        ${_tests}/MaterialPreviewTests.cpp
        ${_tests}/ThumbnailTests.cpp
        ${_tests}/ModelingTests.cpp
        ${_tests}/ModuleConflictsTests.cpp
        ${_tests}/ModuleManagerTests.cpp
        ${_tests}/PickingTests.cpp
        ${_tests}/PluginAuthoringTests.cpp
        ${_tests}/PrefabsTests.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/ScenePlayTests.cpp
        ${_tests}/UndoStackTests.cpp)
target_compile_definitions(interi_editor_tests PRIVATE
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules"
    INTERI_PLUGIN_STUB_DIR="${PROJECT_SOURCE_DIR}/tests/editor/plugin_stubs")

if(INTERI_BUILD_SHARED)
    # The starter sample's module is what the editor hosts in EditorModulesTests.
    add_dependencies(interi_editor_tests com_interi_starter)
    target_compile_definitions(interi_editor_tests PRIVATE
        INTERI_STARTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/samples/modules/starter"
        INTERI_STARTER_LIBRARY_DIR="$<TARGET_FILE_DIR:com_interi_starter>")

    include(${PROJECT_SOURCE_DIR}/cmake/InteriGameplayModule.cmake)
    # Generated Plugin stubs (PluginAuthoringTests pins the generator's output to these files): they must build, load
    # on top of the starter and override its hook and event.
    foreach(_kind hook event)
        string(SUBSTRING ${_kind} 0 1 _first)
        string(TOUPPER ${_first} _first)
        string(SUBSTRING ${_kind} 1 -1 _rest)
        set(_Kind ${_first}${_rest})
        set(_dir ${_tests}/plugin_stubs/${_kind})
        set(_lib com_interi_test_${_kind}plugin)
        interi_add_gameplay_module(${_lib} SOURCES ${_dir}/src/${_Kind}PluginModule.cpp)
        add_test(NAME PluginStub_${_kind}_LoadsOverStarter
            COMMAND interi_module_check --search $<TARGET_FILE_DIR:com_interi_starter>
                    --search $<TARGET_FILE_DIR:${_lib}> ${_dir} ${PROJECT_SOURCE_DIR}/samples/modules/starter)
        # Both load the starter's library; the host's shadow copies are named by library and a per-process counter, so two
        # processes at once would copy over each other's file.
        set_tests_properties(PluginStub_${_kind}_LoadsOverStarter PROPERTIES LABELS integration
            RESOURCE_LOCK module_shadow_copy
            PASS_REGULAR_EXPRESSION "${_kind} com.interi.starter:[A-Za-z]+: com.interi.test.${_kind}plugin \\(override\\) com.interi.starter")
    endforeach()
endif()
target_include_directories(interi_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/chain)
