# SPDX-License-Identifier: MIT
# interi_module_check: loads a project of gameplay modules and reports what they contribute (installed with the
# engine; scaffolded modules run it as their load test).
add_executable(interi_module_check main.cpp)
target_link_libraries(interi_module_check PRIVATE interi::hotreload interi::warnings)
add_executable(interi::module_check ALIAS interi_module_check)
set_target_properties(interi_module_check PROPERTIES EXPORT_NAME module_check)

# The shared SDK test project through the in-tree engine: the Plugin and B both override A's Fire.
if(INTERI_BUILD_SHARED)
    add_test(NAME ModuleCheck_SdkTestProject_Loads
        COMMAND interi_module_check --search $<TARGET_FILE_DIR:interi_sdk_test_a>
                ${PROJECT_SOURCE_DIR}/tests/sdk/modules/plugin ${PROJECT_SOURCE_DIR}/tests/sdk/modules/b
                ${PROJECT_SOURCE_DIR}/tests/sdk/modules/a ${PROJECT_SOURCE_DIR}/tests/sdk/modules/ext)
    set_tests_properties(ModuleCheck_SdkTestProject_Loads PROPERTIES
        LABELS unit
        PASS_REGULAR_EXPRESSION "hook com.test.a:Fire: com.test.plugin \\(override\\) com.test.b \\(override\\) com.test.a")
endif()
