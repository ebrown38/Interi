# SPDX-License-Identifier: MIT
# Ci_PendingLabelFilter_ExcludesSample: the label filter used by presets/CI must hide `pending`
# tests, and `-L pending` must still select them.
execute_process(COMMAND "${CTEST}" -N -LE pending WORKING_DIRECTORY "${BUILD_DIR}"
    OUTPUT_VARIABLE excluded RESULT_VARIABLE rc1)
execute_process(COMMAND "${CTEST}" -N -L pending WORKING_DIRECTORY "${BUILD_DIR}"
    OUTPUT_VARIABLE included RESULT_VARIABLE rc2)
if(NOT rc1 EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "ctest -N failed (${rc1}, ${rc2})")
endif()
if(excluded MATCHES "PendingSample\\.")
    message(FATAL_ERROR "pending sample was NOT excluded by -LE pending:\n${excluded}")
endif()
if(NOT included MATCHES "PendingSample\\.DeliberatelyFails_ExcludedFromDefaultRuns")
    message(FATAL_ERROR "pending sample was not selected by -L pending:\n${included}")
endif()
message(STATUS "pending label filter OK")
