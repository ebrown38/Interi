# SPDX-License-Identifier: MIT
# Script mode helper for the `coverage` target: merges every .profraw under PROFILE_DIR.
file(GLOB _raw "${PROFILE_DIR}/*.profraw")
if(NOT _raw)
    message(FATAL_ERROR "No .profraw files in ${PROFILE_DIR}; run ctest with the coverage preset first")
endif()
execute_process(COMMAND "${PROFDATA}" merge -sparse ${_raw} -o "${OUTPUT}" COMMAND_ERROR_IS_FATAL ANY)
