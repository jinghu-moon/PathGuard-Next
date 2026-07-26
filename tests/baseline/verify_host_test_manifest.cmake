if(NOT DEFINED BUILD_DIR OR NOT DEFINED TEST_MANIFEST OR NOT DEFINED CTEST_COMMAND)
  message(FATAL_ERROR "BUILD_DIR, TEST_MANIFEST and CTEST_COMMAND are required")
endif()

if(NOT DEFINED CONFIG OR NOT CONFIG STREQUAL "Release")
  message(FATAL_ERROR "RF0 host baseline must be verified with CONFIG=Release")
endif()

execute_process(
  COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}" -C "${CONFIG}" -N
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE test_listing
  ERROR_VARIABLE list_error
)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "ctest -N failed: ${list_error}")
endif()

file(STRINGS "${TEST_MANIFEST}" expected_lines)
set(expected_tests)
foreach(line IN LISTS expected_lines)
  string(STRIP "${line}" line)
  if(NOT line STREQUAL "" AND NOT line MATCHES "^#")
    list(APPEND expected_tests "${line}")
  endif()
endforeach()
if(DEFINED INJECT_REQUIRED AND NOT INJECT_REQUIRED STREQUAL "")
  list(APPEND expected_tests "${INJECT_REQUIRED}")
endif()

list(LENGTH expected_tests expected_count)
if(expected_count LESS 10)
  message(FATAL_ERROR "RF0 manifest must protect at least the original 10 tests")
endif()

string(REGEX MATCHALL "Test +#[0-9]+:" listed_tests "${test_listing}")
list(LENGTH listed_tests listed_count)
if(listed_count LESS expected_count)
  message(FATAL_ERROR
    "CTest count regressed: listed=${listed_count}, required=${expected_count}")
endif()

foreach(test_name IN LISTS expected_tests)
  string(FIND "${test_listing}" ": ${test_name}" found_at)
  if(found_at EQUAL -1)
    message(FATAL_ERROR "Required Host test is missing: ${test_name}")
  endif()
endforeach()

string(JOIN "|" expected_pattern ${expected_tests})
execute_process(
  COMMAND "${CTEST_COMMAND}" --test-dir "${BUILD_DIR}" -C "${CONFIG}"
    -R "^(${expected_pattern})$" --output-on-failure
  RESULT_VARIABLE baseline_result
  OUTPUT_VARIABLE baseline_output
  ERROR_VARIABLE baseline_error
)
if(NOT baseline_result EQUAL 0)
  message(FATAL_ERROR
    "RF0 Release Host baseline failed:\n${baseline_output}\n${baseline_error}")
endif()
