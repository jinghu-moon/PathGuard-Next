if(NOT DEFINED BENCHMARK)
  message(FATAL_ERROR "BENCHMARK is required")
endif()

execute_process(
  COMMAND "${BENCHMARK}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "runtime benchmark failed: ${result}\n${error}")
endif()

string(REPLACE "\r\n" "\n" output "${output}")
string(REPLACE "\n" ";" lines "${output}")
set(line_count 0)
foreach(line IN LISTS lines)
  if(line STREQUAL "")
    continue()
  endif()
  string(JSON schema ERROR_VARIABLE schema_error GET "${line}" schema)
  if(schema_error OR NOT schema STREQUAL "pathguard.runtime-benchmark.v1")
    message(FATAL_ERROR "invalid runtime benchmark JSONL schema: ${line}")
  endif()
  string(JSON threads_type ERROR_VARIABLE threads_error TYPE
    "${line}" hardware_threads)
  if(threads_error OR NOT threads_type STREQUAL "NUMBER")
    message(FATAL_ERROR "hardware_threads is not numeric: ${line}")
  endif()
  math(EXPR line_count "${line_count} + 1")
endforeach()

if(NOT line_count EQUAL 8)
  message(FATAL_ERROR "expected 8 runtime benchmark records, got ${line_count}")
endif()
