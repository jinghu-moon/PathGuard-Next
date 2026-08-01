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
  { select = { root = "Pictures", glob = "**/*.jpg", except = ["**/thumbnail-*/**"], type = "file" } },
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
   OR NOT explain_output MATCHES "except=\\*\\*/thumbnail-\\*/\\*\\*"
   OR NOT explain_output MATCHES "content_generation="
   OR NOT explain_output MATCHES "required_operations=")
  message(FATAL_ERROR "v6 explain contract failed: ${explain_error}\n${explain_output}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" explain "${work}/policy.bin" com.example.cli --json
  RESULT_VARIABLE explain_json_result OUTPUT_VARIABLE explain_json_output
  ERROR_VARIABLE explain_json_error)
if(NOT explain_json_result EQUAL 0
   OR NOT explain_json_output MATCHES "\"schema\":\"pathguard.explain.v1\""
   OR NOT explain_json_output MATCHES "\"admission\":\"not_evaluated\""
   OR NOT explain_json_output MATCHES "\"action\":\"observe\""
   OR NOT explain_json_output MATCHES "\"domain\":\"event\""
   OR NOT explain_json_output MATCHES "\"except\":"
   OR NOT explain_json_output MATCHES "thumbnail-"
   OR NOT explain_json_output MATCHES "\"required_operations\":")
  message(FATAL_ERROR "v6 JSON explain contract failed: ${explain_json_error}\n${explain_json_output}")
endif()

file(WRITE "${work}/module/run/rules-status.txt"
  "status_version=6\ncapability_generation=4\nbit17=unsupported\n")
file(WRITE "${work}/module/run/rules-status.json"
  "{\"schema\":\"pathguard.rules_status.v1\",\"version\":1,\"status\":\"active\",\"capability_generation\":4}\n")
file(WRITE "${work}/module/run/status/42.status"
  "schema=pathguard.runtime_status.v2\nversion=2\npid=42\nprocess=com.example.cli\nenforcement=active\nerror=0\ncapability_generation=4\nobserved_capabilities=16\naction_count=1\naction_total=1\nactions_truncated=false\nhazard_slot_acquire_fail_total=0\nevent_overflow_total=2\ndiagnostic_drop_total=3\naction.0.kind=redirect\naction.0.domain=provider\naction.0.admission=active\naction.0.required_capabilities=16\naction.0.observed_capabilities=16\naction.0.missing_capabilities=0\naction.0.required_operations=2\naction.0.observed_operations=2\naction.0.missing_operations=0\naction.0.rule_id=41\naction.0.selector_id=7\nattempt=01\n")
execute_process(
  COMMAND "${PATHGUARDCTL}" status "${work}/module"
  RESULT_VARIABLE status_result OUTPUT_VARIABLE status_output)
if(NOT status_result EQUAL 0
   OR NOT status_output MATCHES "capability_generation=4"
   OR NOT status_output MATCHES "bit17=unsupported")
  message(FATAL_ERROR "v6 status contract failed: ${status_output}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" status "${work}/module" --json
  RESULT_VARIABLE status_json_result OUTPUT_VARIABLE status_json_output
  ERROR_VARIABLE status_json_error)
if(NOT status_json_result EQUAL 0
   OR NOT status_json_output MATCHES "\"schema\":\"pathguard.status.v1\""
   OR NOT status_json_output MATCHES "pathguard.rules_status.v1"
   OR NOT status_json_output MATCHES "pathguard.runtime_status.v2"
   OR NOT status_json_output MATCHES "\"capability_generation\":4"
   OR NOT status_json_output MATCHES "\"pid\":42"
   OR NOT status_json_output MATCHES "\"process\":\"com.example.cli\""
   OR NOT status_json_output MATCHES "\"enforcement\":\"active\""
   OR NOT status_json_output MATCHES "\"action_count\":1"
   OR NOT status_json_output MATCHES "\"action.0.kind\":\"redirect\""
   OR NOT status_json_output MATCHES "\"action.0.domain\":\"provider\""
   OR NOT status_json_output MATCHES "\"action.0.admission\":\"active\""
   OR NOT status_json_output MATCHES "\"action.0.rule_id\":41"
   OR NOT status_json_output MATCHES "\"event_overflow_total\":2"
   OR NOT status_json_output MATCHES "\"diagnostic_drop_total\":3"
   OR NOT status_json_output MATCHES "\"attempt\":\"01\"")
  message(FATAL_ERROR "v6 JSON status contract failed: ${status_json_error}\n${status_json_output}")
endif()

execute_process(
  COMMAND "${PATHGUARDCTL}" status "${work}/module" 42 --json
  RESULT_VARIABLE process_json_result OUTPUT_VARIABLE process_json_output)
if(NOT process_json_result EQUAL 0
   OR NOT process_json_output MATCHES "\"pid\":42"
   OR NOT process_json_output MATCHES "\"error\":0")
  message(FATAL_ERROR "v6 process JSON status failed: ${process_json_output}")
endif()
