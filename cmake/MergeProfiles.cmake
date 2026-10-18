# SPDX-License-Identifier: MIT
# Script mode helper for the `coverage` target: merges every .profraw under PROFILE_DIR.
file(GLOB _raw "${PROFILE_DIR}/*.profraw")
if(NOT _raw)
    message(FATAL_ERROR "No .profraw files in ${PROFILE_DIR}; run ctest with the coverage preset first")
endif()
# Pass the files through a list: thousands of profiles (one per test process) overflow the command line.
list(JOIN _raw "\n" _list)
file(WRITE "${OUTPUT}.list" "${_list}\n")
execute_process(COMMAND "${PROFDATA}" merge -sparse -f "${OUTPUT}.list" -o "${OUTPUT}" COMMAND_ERROR_IS_FATAL ANY)
