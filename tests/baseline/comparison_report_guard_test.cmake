if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR are required")
endif()

set(verifier "${SOURCE_DIR}/tests/baseline/validate_comparison_report.cmake")
include("${SOURCE_DIR}/tests/baseline/comparison_report_schema_v1.cmake")
set(valid_report
  "${SOURCE_DIR}/tests/fixtures/comparison-reports/valid-report.json")
set(temp_root "${BINARY_DIR}/comparison-report-guard-tmp")
file(MAKE_DIRECTORY "${temp_root}")
file(READ "${valid_report}" valid_json)

set(failures)
foreach(field IN LISTS PATHGUARD_COMPARISON_REPORT_REQUIRED_FIELDS)
  string(JSON invalid_json REMOVE "${valid_json}" "${field}")
  set(invalid_report "${temp_root}/missing-${field}.json")
  file(WRITE "${invalid_report}" "${invalid_json}\n")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DREPORT=${invalid_report}" -P "${verifier}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  set(combined "${output}\n${error}")
  set(expected "missing_field:${field}")
  if(result EQUAL 0)
    list(APPEND failures "${field}: invalid report was accepted")
  elseif(NOT combined MATCHES "${expected}")
    list(APPEND failures "${field}: expected diagnostic ${expected}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DREPORT=${valid_report}" -P "${verifier}"
  RESULT_VARIABLE valid_result
  OUTPUT_VARIABLE valid_output
  ERROR_VARIABLE valid_error
)
if(NOT valid_result EQUAL 0)
  list(APPEND failures "valid report was rejected")
endif()

set(invalid_classification "${valid_json}")
string(REPLACE
  "\"classification\": \"unchanged\""
  "\"classification\": \"invalid\""
  invalid_classification "${invalid_classification}")
set(invalid_classification_report "${temp_root}/invalid-classification.json")
file(WRITE "${invalid_classification_report}" "${invalid_classification}\n")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    "-DREPORT=${invalid_classification_report}" -P "${verifier}"
  RESULT_VARIABLE classification_result
  OUTPUT_VARIABLE classification_output
  ERROR_VARIABLE classification_error
)
if(classification_result EQUAL 0)
  list(APPEND failures "invalid classification was accepted")
elseif(NOT "${classification_output}\n${classification_error}"
    MATCHES "invalid_classification")
  list(APPEND failures
    "classification: expected diagnostic invalid_classification")
endif()

set(historical_report
  "${SOURCE_DIR}/tests/baseline/pattern-v6/p6-bootstrap-20260729/V-10-route-provenance-decision.md")
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DREPORT=${historical_report}" -P "${verifier}"
  RESULT_VARIABLE historical_result
  OUTPUT_VARIABLE historical_output
  ERROR_VARIABLE historical_error
)
if(historical_result EQUAL 0)
  list(APPEND failures "historical Markdown report was accepted as format 1 JSON")
elseif(NOT "${historical_output}\n${historical_error}"
    MATCHES "unsupported_report_format")
  list(APPEND failures
    "historical report: expected diagnostic unsupported_report_format")
endif()

if(failures)
  string(JOIN "\n  - " failure_text ${failures})
  message(FATAL_ERROR
    "comparison report contract is not implemented:\n  - ${failure_text}")
endif()
