if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(verifier "${SOURCE_DIR}/tests/baseline/validate_rules_d0.cmake")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${SOURCE_DIR}" -P "${verifier}"
  RESULT_VARIABLE real_result
  OUTPUT_VARIABLE real_output
  ERROR_VARIABLE real_error
)
if(NOT real_result EQUAL 0)
  message(FATAL_ERROR
    "Real D0 evidence failed validation:\n${real_output}\n${real_error}")
endif()

foreach(injection IN ITEMS
    INJECT_MISSING_CANDIDATE INJECT_BAD_DECISION INJECT_RUST_ARTIFACT)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-D${injection}=ON"
      -P "${verifier}"
    RESULT_VARIABLE injected_result
  )
  if(injected_result EQUAL 0)
    message(FATAL_ERROR "D0 guard accepted injected failure: ${injection}")
  endif()
endforeach()
