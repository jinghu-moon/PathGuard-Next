if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR "SOURCE_DIR and OUTPUT_DIR are required")
endif()

set(verifier "${SOURCE_DIR}/tests/baseline/validate_comparison_report.cmake")
set(valid_report
  "${SOURCE_DIR}/tests/baseline/pattern-v6/p6-bootstrap-20260729/V-11-valid-comparison-report.json")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(READ "${valid_report}" valid_json)

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DREPORT=${valid_report}" -P "${verifier}"
  RESULT_VARIABLE valid_result
  OUTPUT_VARIABLE valid_output
  ERROR_VARIABLE valid_error
)
if(NOT valid_result EQUAL 0)
  message(FATAL_ERROR "valid V-11 report failed: ${valid_output}${valid_error}")
endif()

string(JSON missing_commit_json REMOVE "${valid_json}" before_commit)
set(missing_commit "${OUTPUT_DIR}/missing-before-commit.json")
file(WRITE "${missing_commit}" "${missing_commit_json}\n")

string(JSON invalid_class_json
  SET "${valid_json}" classification "\"invalid\"")
set(invalid_class "${OUTPUT_DIR}/invalid-classification.json")
file(WRITE "${invalid_class}" "${invalid_class_json}\n")

string(JSON empty_evidence_json SET "${valid_json}" evidence_paths "[]")
set(empty_evidence "${OUTPUT_DIR}/empty-evidence.json")
file(WRITE "${empty_evidence}" "${empty_evidence_json}\n")

set(cases
  "${missing_commit}|missing_field:before_commit"
  "${invalid_class}|invalid_classification"
  "${empty_evidence}|empty_field:evidence_paths"
)
set(result_lines "valid|accepted")
foreach(case IN LISTS cases)
  string(REPLACE "|" ";" case_parts "${case}")
  list(GET case_parts 0 report)
  list(GET case_parts 1 expected_reason)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DREPORT=${report}" -P "${verifier}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  set(combined "${output}\n${error}")
  if(result EQUAL 0 OR NOT combined MATCHES "${expected_reason}")
    message(FATAL_ERROR
      "${report} did not fail with ${expected_reason}: ${combined}")
  endif()
  get_filename_component(name "${report}" NAME)
  list(APPEND result_lines "${name}|${expected_reason}")
endforeach()

string(JOIN "\n" result_text ${result_lines})
file(WRITE "${OUTPUT_DIR}/v11-results.txt" "${result_text}\n")
message(STATUS "V-11 comparison report replay passed\n${result_text}")
