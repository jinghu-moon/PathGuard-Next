if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED CTEST_COMMAND)
  message(FATAL_ERROR "SOURCE_DIR, BUILD_DIR and CTEST_COMMAND are required")
endif()

set(verifier "${SOURCE_DIR}/tests/baseline/verify_host_test_manifest.cmake")
set(manifest "${SOURCE_DIR}/tests/baseline/expected-host-tests.txt")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${BUILD_DIR}"
    "-DTEST_MANIFEST=${manifest}"
    "-DCTEST_COMMAND=${CTEST_COMMAND}"
    "-DCONFIG=Release"
    -P "${verifier}"
  RESULT_VARIABLE baseline_result
  OUTPUT_VARIABLE baseline_output
  ERROR_VARIABLE baseline_error
)
if(NOT baseline_result EQUAL 0)
  message(FATAL_ERROR
    "The real RF0 baseline did not pass:\n${baseline_output}\n${baseline_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${BUILD_DIR}"
    "-DTEST_MANIFEST=${manifest}"
    "-DCTEST_COMMAND=${CTEST_COMMAND}"
    "-DCONFIG=Release"
    "-DINJECT_REQUIRED=pathguard_intentionally_missing_test"
    -P "${verifier}"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_output
  ERROR_VARIABLE missing_error
)
if(missing_result EQUAL 0)
  message(FATAL_ERROR
    "RF0 baseline guard did not reject an intentionally missing test")
endif()
