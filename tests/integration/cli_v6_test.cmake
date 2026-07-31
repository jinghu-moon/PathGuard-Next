if(NOT DEFINED PATHGUARDCTL OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "PATHGUARDCTL and TEST_ROOT are required")
endif()

set(work "${TEST_ROOT}/cli-v6")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}/module/run/status")
file(WRITE "${work}/rules.toml" [=[format = 2
[apps."com.example.cli"]
users = [0]
observe_rules = [
  { select = { root = "Pictures", glob = "**/*.jpg", type = "file" } },
]
export_rules = [
  { select = { root = "Pictures", glob = "**/*.png", type = "file" }, to = "Download/export", mode = "copy" },
]
]=])

execute_process(
  COMMAND "${PATHGUARDCTL}" compile "${work}/rules.toml" "${work}/policy.bin"
  RESULT_VARIABLE compile_result OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_error)
if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR "v6 compile failed: ${compile_error}")
endif()
execute_process(
  COMMAND "${PATHGUARDCTL}" explain "${work}/policy.bin" com.example.cli
  RESULT_VARIABLE explain_result OUTPUT_VARIABLE explain_output
  ERROR_VARIABLE explain_error)
if(NOT explain_result EQUAL 0
   OR NOT explain_output MATCHES "action=observe domain=event"
   OR NOT explain_output MATCHES "action=export domain=event"
   OR NOT explain_output MATCHES "glob=\\*\\*/\\*\\.jpg"
   OR NOT explain_output MATCHES "content_generation="
   OR NOT explain_output MATCHES "required_operations=")
  message(FATAL_ERROR "v6 explain contract failed: ${explain_error}\n${explain_output}")
endif()

file(WRITE "${work}/module/run/rules-status.txt"
  "status_version=6\ncapability_generation=4\nbit17=unsupported\n")
execute_process(
  COMMAND "${PATHGUARDCTL}" status "${work}/module"
  RESULT_VARIABLE status_result OUTPUT_VARIABLE status_output)
if(NOT status_result EQUAL 0
   OR NOT status_output MATCHES "capability_generation=4"
   OR NOT status_output MATCHES "bit17=unsupported")
  message(FATAL_ERROR "v6 status contract failed: ${status_output}")
endif()
