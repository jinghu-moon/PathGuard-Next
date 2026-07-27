if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(verifier "${SOURCE_DIR}/tests/integration/rules/rules_build_boundary_test.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}" -P "${verifier}"
  RESULT_VARIABLE real_result
)
if(NOT real_result EQUAL 0)
  message(FATAL_ERROR "real compiler boundary failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}"
    -DINJECT_ZYGISK_COMPILER=ON -P "${verifier}"
  RESULT_VARIABLE injected_result
)
if(injected_result EQUAL 0)
  message(FATAL_ERROR "boundary guard accepted injected Zygisk compiler source")
endif()
