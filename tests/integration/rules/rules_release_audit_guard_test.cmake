if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()
set(verifier "${SOURCE_DIR}/tests/integration/rules/rules_release_audit_test.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}" -P "${verifier}"
  RESULT_VARIABLE real_result)
if(NOT real_result EQUAL 0)
  message(FATAL_ERROR "real RF8 release audit failed")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}"
    -DINJECT_FORBIDDEN=ON -P "${verifier}"
  RESULT_VARIABLE injected_result)
if(injected_result EQUAL 0)
  message(FATAL_ERROR "RF8 release audit accepted injected forbidden artifact")
endif()
