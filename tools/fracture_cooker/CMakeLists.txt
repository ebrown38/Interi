# SPDX-License-Identifier: MIT
# fracture_cooker: command-line front end of interi::destruction_cook (glTF building -> .idest destructible).
add_executable(fracture_cooker main.cpp)
target_link_libraries(fracture_cooker PRIVATE interi::destruction_cook interi::jobs interi::warnings)

# Smoke test: cook the sample building end to end.
add_test(NAME FractureCooker_SampleBuilding_Cooks
    COMMAND fracture_cooker ${PROJECT_SOURCE_DIR}/data/destruction/samples/building.gltf
            -o ${CMAKE_CURRENT_BINARY_DIR}/building.idest --force)
set_tests_properties(FractureCooker_SampleBuilding_Cooks PROPERTIES
    LABELS integration
    PASS_REGULAR_EXPRESSION "cooked .* chunks")
add_test(NAME FractureCooker_MissingInput_Fails COMMAND fracture_cooker does-not-exist.gltf)
set_tests_properties(FractureCooker_MissingInput_Fails PROPERTIES LABELS integration WILL_FAIL TRUE)
